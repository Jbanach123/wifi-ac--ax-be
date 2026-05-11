/*
 *
 * Topology:
 *
 *   [STA_voip_0]  [STA_voip_1] ... [STA_voip_N]        <- VoIP stations (EDCA AC_VO)
 *          \            |               /
 *           \           |              /
 *                    [AP]
 *           /           |              \
 *          /            |               \
 *   [STA_bg_0]   [STA_bg_1]  ...  [STA_bg_M]           <- background stations (EDCA AC_BE)
 *
 * Each VoIP station generates:
 *   - uplink UDP flow  (STA → AP)  G.711: 160 B / 20 ms = 64 kbps
 *   - downlink UDP flow (AP → STA) same codec
 *   Both flows carry ToS = 0xb8 (DSCP EF) → mapped to EDCA AC_VO by ns-3.
 *
 * Each background station generates:
 *   - uplink TCP flow  (STA → AP)  ~2 Mbps, ToS = 0x00 → EDCA AC_BE
 *
 * Measured metrics (per flow, via FlowMonitor):
 *   mean delay [ms], mean jitter [ms], packet loss [%], throughput [kbps]
 *
 * QoS thresholds for VoIP (ITU-T G.114):
 *   delay  < 150 ms,  jitter < 30 ms,  loss < 1 %
 *
 * Usage:
 *   ./ns3 run "scratch/voip-comparison --standard=ax --nVoip=5 --nBackground=3 --runSeed=1"
 *   ./ns3 run "scratch/voip-comparison --standard=ac --nVoip=5 --nBackground=3 --runSeed=1"
 *
 * Redirect output to CSV for further analysis:
 *   ./ns3 run "scratch/voip-comparison ..." >> results.csv
 */
 
// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — INCLUDES
// ─────────────────────────────────────────────────────────────────────────────
// Standard C++ math (M_PI, std::cos, std::sin used in position allocator)
#include <cmath>
 
// ns-3 core helpers
#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
 
// ns-3 network / internet
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
 
// ns-3 Wi-Fi (Yans PHY — single-channel, sufficient for ac/ax comparison)
#include "ns3/ssid.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"
 
// ns-3 mobility
#include "ns3/mobility-helper.h"
 
// ns-3 applications
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
 
// ns-3 FlowMonitor — collects per-flow delay, jitter, loss, throughput
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
 
using namespace ns3;
 
// Log component name matches the file — used with NS_LOG=voip-comparison
NS_LOG_COMPONENT_DEFINE("voip-comparison");
 
// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — EXPERIMENT PARAMETERS
// All parameters are exposed via CommandLine so no recompilation is needed
// between experiment runs.
// ─────────────────────────────────────────────────────────────────────────────
int
main(int argc, char* argv[])
{
    // --- independent variables (change between experiment runs) ---
    std::string standard{"ax"};   // Wi-Fi standard: "ac" (802.11ac) or "ax" (802.11ax)
    std::size_t nVoip{5};         // number of VoIP stations — primary swept variable
    std::size_t nBackground{3};   // number of background (BE) stations
    bool withBackground{true};    // false → run without background load (baseline)
    uint32_t runSeed{1};          // RNG seed; change to 1,2,3 for independent replications
 
    // --- fixed scenario parameters ---
    double frequency{5};          // operating band: 5 GHz (or 2.4)
    int channelWidth{80};         // channel width in MHz: 20 / 40 / 80
    Time simulationTime{"30s"};   // must be long enough for TCP to reach steady state
    double distance{10.0};        // AP-to-STA distance in metres (all stations equal)
 
    // --- VoIP traffic model: G.711 codec ---
    // 160 bytes payload, 20 ms packetisation interval → 64 000 bps per direction
    uint32_t voipPayload{160};    // bytes per packet
    double   voipInterval{0.02};  // inter-packet gap in seconds
 
    // --- background traffic model ---
    // Constant-rate TCP uplink, saturates the channel alongside VoIP
    std::string bgDataRate{"2Mbps"};
    uint32_t    bgPayload{1460};  // bytes (standard Ethernet MSS)
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 3 — COMMAND-LINE INTERFACE
    // ─────────────────────────────────────────────────────────────────────────
    CommandLine cmd(__FILE__);
    cmd.AddValue("standard",       "Wi-Fi standard: ac or ax",              standard);
    cmd.AddValue("nVoip",          "Number of VoIP stations",               nVoip);
    cmd.AddValue("nBackground",    "Number of background (BE) stations",    nBackground);
    cmd.AddValue("frequency",      "Operating band in GHz (5 or 2.4)",      frequency);
    cmd.AddValue("channelWidth",   "Channel width in MHz (20 / 40 / 80)",   channelWidth);
    cmd.AddValue("simulationTime", "Simulation duration (e.g. 30s)",        simulationTime);
    cmd.AddValue("distance",       "AP-to-STA distance in metres",          distance);
    cmd.AddValue("withBackground", "Enable background traffic (true/false)", withBackground);
    cmd.AddValue("runSeed",        "RNG seed for independent replications",  runSeed);
    cmd.Parse(argc, argv);
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 4 — RNG SEED
    // Ensures reproducibility: same seed → identical result every run.
    // Use different seeds (1, 2, 3 …) to produce independent replications
    // and compute confidence intervals.
    // ─────────────────────────────────────────────────────────────────────────
    Config::SetGlobal("RngSeed", UintegerValue(runSeed));
    Config::SetGlobal("RngRun",  UintegerValue(runSeed));
 
    // Resolve background station count (0 when background is disabled)
    std::size_t nBg = withBackground ? nBackground : 0;
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 5 — NODE CONTAINERS
    // Separate containers for AP, VoIP stations, and background stations so
    // that different applications and ToS values can be installed on each group.
    // ─────────────────────────────────────────────────────────────────────────
    NodeContainer wifiApNode;
    wifiApNode.Create(1);              // single access point
 
    NodeContainer wifiVoipNodes;
    wifiVoipNodes.Create(nVoip);      // VoIP stations → will get AC_VO traffic
 
    NodeContainer wifiBgNodes;
    wifiBgNodes.Create(nBg);          // background stations → will get AC_BE traffic
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 6 — WI-FI STANDARD SELECTION
    // Both standards use ConstantRateWifiManager at MCS 7 so the modulation
    // order is identical — the comparison isolates MAC-layer differences
    // (frame aggregation limits, EDCA parameters, TXOP durations).
    // ─────────────────────────────────────────────────────────────────────────
    WifiHelper wifi;
    WifiMacHelper mac;
 
    if (standard == "ax")
    {
        // 802.11ax (Wi-Fi 6) — HE PHY, HE MCS 7 ≈ 64-QAM 5/6
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",    StringValue("HeMcs7"),
                                     "ControlMode", StringValue("OfdmRate24Mbps"));
        // 800 ns GI: shortest guard interval, highest efficiency
        wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(800)));
    }
    else if (standard == "ac")
    {
        // 802.11ac (Wi-Fi 5) — VHT PHY, VHT MCS 7 ≈ 64-QAM 5/6
        wifi.SetStandard(WIFI_STANDARD_80211ac);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",    StringValue("VhtMcs7"),
                                     "ControlMode", StringValue("OfdmRate24Mbps"));
        // ConfigHeOptions() is 802.11ax-only; ac defaults to 800 ns GI
    }
    else
    {
        NS_FATAL_ERROR("Unknown standard: " << standard << " — use 'ac' or 'ax'");
    }
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 7 — CHANNEL CONFIGURATION
    // Channel string format for phy.Set("ChannelSettings", ...):
    //   {<channel_number>, <width_MHz>, <band>, <primary_20_index>}
    //   channel_number = 0 → ns-3 picks the default channel for the band
    // ─────────────────────────────────────────────────────────────────────────
    std::string channelStr("{0, " + std::to_string(channelWidth) + ", ");
    if (frequency == 5)
        channelStr += "BAND_5GHZ, 0}";
    else if (frequency == 2.4)
        channelStr += "BAND_2_4GHZ, 0}";
    else
        NS_FATAL_ERROR("Unsupported frequency: " << frequency << " — use 5 or 2.4");
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 8 — PROPAGATION MODEL
    // Log-distance model with exponent n=3.0 represents an indoor office
    // environment (walls, furniture).  Reference values:
    //   5 GHz:   L0 = 46.67 dB at d0 = 1 m  (IEEE 802.11 TGax channel model D)
    //   2.4 GHz: L0 = 40.0  dB at d0 = 1 m
    // ─────────────────────────────────────────────────────────────────────────
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                               "Exponent",          DoubleValue(3.0),
                               "ReferenceDistance", DoubleValue(1.0),
                               "ReferenceLoss",
                               DoubleValue(frequency == 5 ? 46.67 : 40.0));
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 9 — PHY AND MAC INSTALLATION
    // Yans (Yet Another Network Simulator) PHY: single-channel model,
    // adequate for ac/ax comparison without OFDMA (SU-only scenario).
    // All stations and the AP share the same SSID and BSS.
    // ─────────────────────────────────────────────────────────────────────────
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("ChannelSettings", StringValue(channelStr));
 
    Ssid ssid = Ssid("voip-bss");
 
    // Install STA MAC on VoIP nodes
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer voipDevices = wifi.Install(phy, mac, wifiVoipNodes);
 
    // Install STA MAC on background nodes (same BSS)
    NetDeviceContainer bgDevices = wifi.Install(phy, mac, wifiBgNodes);
 
    // Install AP MAC
    mac.SetType("ns3::ApWifiMac",
                "Ssid",               SsidValue(ssid),
                "EnableBeaconJitter", BooleanValue(false)); // deterministic beacons
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 10 — MOBILITY (static positions)
    // AP is placed at the origin.
    // VoIP stations are distributed evenly on a circle of radius `distance`
    // so every VoIP link has the same path loss — controlled experiment.
    // Background stations are placed on a smaller circle (70 % of distance)
    // to reflect that they are not at the cell edge.
    // ─────────────────────────────────────────────────────────────────────────
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
 
    posAlloc->Add(Vector(0.0, 0.0, 0.0)); // AP at origin
 
    // VoIP stations — uniformly around AP at radius `distance`
    for (std::size_t i = 0; i < nVoip; i++)
    {
        double angle = 2.0 * M_PI * i / nVoip;
        posAlloc->Add(Vector(distance * std::cos(angle),
                             distance * std::sin(angle),
                             0.0));
    }
 
    // Background stations — uniformly around AP at 0.7 × distance
    for (std::size_t i = 0; i < nBg; i++)
    {
        double angle = 2.0 * M_PI * i / (nBg > 0 ? nBg : 1);
        posAlloc->Add(Vector(0.7 * distance * std::cos(angle),
                             0.7 * distance * std::sin(angle),
                             0.0));
    }
 
    mobility.SetPositionAllocator(posAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);
    mobility.Install(wifiVoipNodes);
    mobility.Install(wifiBgNodes);
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 11 — INTERNET STACK AND IP ADDRESSING
    // Single /24 subnet shared by all nodes.
    // Address assignment order: AP → VoIP STAs → background STAs
    // ─────────────────────────────────────────────────────────────────────────
    InternetStackHelper stack;
    stack.Install(wifiApNode);
    stack.Install(wifiVoipNodes);
    stack.Install(wifiBgNodes);
 
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
 
    Ipv4InterfaceContainer apIface    = address.Assign(apDevice);    // 10.1.1.1
    Ipv4InterfaceContainer voipIfaces = address.Assign(voipDevices); // 10.1.1.2 …
    address.Assign(bgDevices);  // background IPs assigned but container not needed
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 12 — VoIP APPLICATIONS
    //
    // Model: G.711 codec
    //   payload  = 160 bytes
    //   interval = 20 ms
    //   bitrate  = 160 * 8 / 0.02 = 64 000 bps per direction
    //
    // ToS = 0xb8 (binary 1011 1000)
    //   DSCP field [7:2] = 101110 = EF (Expedited Forwarding)
    //   ns-3 WifiNetDevice maps ToS bits [7:5] to EDCA AC:
    //     101 (5) → AC_VO  — highest priority, shortest AIFS and CW
    //
    // Each station i gets two flows:
    //   uplink  port 5000 + 2*i   (STA i → AP)
    //   downlink port 5000 + 2*i+1 (AP → STA i)
    // ─────────────────────────────────────────────────────────────────────────
    uint16_t voipPortBase = 5000;
    ApplicationContainer voipApps;
 
    for (std::size_t i = 0; i < nVoip; i++)
    {
        // ── uplink: STA i → AP ───────────────────────────────────────────────
        uint16_t ulPort = voipPortBase + static_cast<uint16_t>(2 * i);
 
        // Sink on AP receives uplink packets
        PacketSinkHelper ulSink("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), ulPort));
        ApplicationContainer ulSinkApp = ulSink.Install(wifiApNode.Get(0));
        ulSinkApp.Start(Seconds(0));
        ulSinkApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(ulSinkApp);
 
        // Source on STA i sends at G.711 rate with AC_VO marking
        OnOffHelper ulSrc("ns3::UdpSocketFactory",
                          InetSocketAddress(apIface.GetAddress(0), ulPort));
        // SetConstantRate(DataRate, packetSize): sets DataRate and PacketSize together
        ulSrc.SetConstantRate(
            DataRate(voipPayload * 8 * static_cast<uint64_t>(1.0 / voipInterval)),
            voipPayload);
        ulSrc.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        ulSrc.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ulSrc.SetAttribute("Tos", UintegerValue(0xb8)); // DSCP EF → EDCA AC_VO
        ApplicationContainer ulSrcApp = ulSrc.Install(wifiVoipNodes.Get(i));
        ulSrcApp.Start(Seconds(1));  // start after AP is ready
        ulSrcApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(ulSrcApp);
 
        // ── downlink: AP → STA i ─────────────────────────────────────────────
        uint16_t dlPort = voipPortBase + static_cast<uint16_t>(2 * i + 1);
 
        // Sink on STA i receives downlink packets
        PacketSinkHelper dlSink("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), dlPort));
        ApplicationContainer dlSinkApp = dlSink.Install(wifiVoipNodes.Get(i));
        dlSinkApp.Start(Seconds(0));
        dlSinkApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(dlSinkApp);
 
        // Source on AP sends downlink VoIP to STA i
        OnOffHelper dlSrc("ns3::UdpSocketFactory",
                          InetSocketAddress(voipIfaces.GetAddress(i), dlPort));
        dlSrc.SetConstantRate(
            DataRate(voipPayload * 8 * static_cast<uint64_t>(1.0 / voipInterval)),
            voipPayload);
        dlSrc.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        dlSrc.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        dlSrc.SetAttribute("Tos", UintegerValue(0xb8)); // DSCP EF → EDCA AC_VO
        ApplicationContainer dlSrcApp = dlSrc.Install(wifiApNode.Get(0));
        dlSrcApp.Start(Seconds(1));
        dlSrcApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(dlSrcApp);
    }
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 13 — BACKGROUND TRAFFIC APPLICATIONS
    //
    // Model: constant-rate TCP uplink (STA → AP), ~2 Mbps each.
    // ToS = 0x00 → DSCP BE → EDCA AC_BE (lowest priority).
    //
    // Purpose: load the wireless medium so that VoIP QoS degrades under
    // contention, allowing us to measure how each standard copes.
    // Each background station uses a unique destination port.
    // ─────────────────────────────────────────────────────────────────────────
    uint16_t bgPortBase = 6000;
    ApplicationContainer bgApps;
 
    for (std::size_t i = 0; i < nBg; i++)
    {
        uint16_t bgPort = bgPortBase + static_cast<uint16_t>(i);
 
        // Sink on AP receives background TCP stream
        PacketSinkHelper bgSink("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), bgPort));
        ApplicationContainer bgSinkApp = bgSink.Install(wifiApNode.Get(0));
        bgSinkApp.Start(Seconds(0));
        bgSinkApp.Stop(simulationTime + Seconds(1));
        bgApps.Add(bgSinkApp);
 
        // Source on background STA — always ON, best-effort class
        OnOffHelper bgSrc("ns3::TcpSocketFactory",
                          InetSocketAddress(apIface.GetAddress(0), bgPort));
        bgSrc.SetAttribute("DataRate",   StringValue(bgDataRate));
        bgSrc.SetAttribute("PacketSize", UintegerValue(bgPayload));
        bgSrc.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        bgSrc.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        bgSrc.SetAttribute("Tos", UintegerValue(0x00)); // DSCP 0 → EDCA AC_BE
        ApplicationContainer bgSrcApp = bgSrc.Install(wifiBgNodes.Get(i));
        bgSrcApp.Start(Seconds(1));
        bgSrcApp.Stop(simulationTime + Seconds(1));
        bgApps.Add(bgSrcApp);
    }
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 14 — ROUTING
    // GlobalRouting computes shortest paths; sufficient for a single-BSS
    // topology where all paths go through the AP.
    // Must be called after IP addresses are assigned.
    // ─────────────────────────────────────────────────────────────────────────
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 15 — FLOW MONITOR
    // Installed on all nodes before Simulator::Run().
    // Collects per-flow statistics automatically during simulation:
    //   txPackets, rxPackets, lostPackets, delaySum, jitterSum, rxBytes
    // ─────────────────────────────────────────────────────────────────────────
    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor> flowMon = flowMonHelper.InstallAll();
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 16 — RUN SIMULATION
    // Extra 1 s added to simulationTime ensures all in-flight packets are
    // delivered and counted before the simulation stops.
    // ─────────────────────────────────────────────────────────────────────────
    Simulator::Stop(simulationTime + Seconds(1));
    Simulator::Run();
 
    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 17 — COLLECT AND PRINT RESULTS
    //
    // Output format: CSV, one row per flow.
    // Redirect to file:  ./ns3 run "..." >> results.csv
    //
    // QoS pass/fail criteria for each UDP (VoIP) flow:
    //   meanDelayMs  < 150   (ITU-T G.114 one-way delay)
    //   meanJitterMs < 30    (typical softphone buffer)
    //   lossPct      < 1.0   (G.711 tolerance threshold)
    // ─────────────────────────────────────────────────────────────────────────
    flowMon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());
 
    // CSV header — printed once per simulation run
    std::cout << "standard,nVoip,nBg,seed,srcAddr,dstAddr,protocol,"
              << "txPkts,rxPkts,lostPkts,lossPct,"
              << "meanDelayMs,meanJitterMs,throughputKbps"
              << std::endl;
 
    auto stats = flowMon->GetFlowStats();
    for (const auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        const FlowMonitor::FlowStats& s = kv.second;
 
        double txPkts  = static_cast<double>(s.txPackets);
        double rxPkts  = static_cast<double>(s.rxPackets);
        double lost    = static_cast<double>(s.lostPackets);
 
        // Packet loss ratio [%]
        double lossPct = (txPkts > 0) ? (lost / txPkts * 100.0) : 0.0;
 
        // Mean end-to-end delay [ms]: sum of delays / number of received packets
        double meanDelayMs = (rxPkts > 0)
                             ? s.delaySum.GetSeconds() / rxPkts * 1000.0
                             : 0.0;
 
        // Mean jitter [ms]: sum of inter-arrival variations / (rxPkts - 1)
        // jitterSum is accumulated as |delay_i - delay_{i-1}| by FlowMonitor
        double meanJitterMs = (rxPkts > 1)
                              ? s.jitterSum.GetSeconds() / (rxPkts - 1) * 1000.0
                              : 0.0;
 
        // Goodput [kbps]: received bytes * 8 bits / simulation duration / 1000
        double throughputKbps =
            (s.rxBytes * 8.0) / simulationTime.GetSeconds() / 1000.0;
 
        // Identify flow type from IP protocol number
        // 17 = UDP → VoIP flow,  6 = TCP → background flow
        std::string proto = (t.protocol == 6) ? "TCP" : "UDP";
 
        std::cout << standard              << ","
                  << nVoip                << ","
                  << nBg                  << ","
                  << runSeed              << ","
                  << t.sourceAddress      << ","
                  << t.destinationAddress << ","
                  << proto                << ","
                  << static_cast<uint64_t>(txPkts)  << ","
                  << static_cast<uint64_t>(rxPkts)  << ","
                  << static_cast<uint64_t>(lost)    << ","
                  << lossPct              << ","
                  << meanDelayMs          << ","
                  << meanJitterMs         << ","
                  << throughputKbps
                  << std::endl;
    }
 
    Simulator::Destroy();
    return 0;
}
