/*
 * ac-ax-be-comparison_fix.cc
 *
 * UNIFIED VoIP QoS SIMULATION
 *
 * SCENARIOS:
 * 1. Collision reduction (RTS/CTS): --scenario=1 --rtsCtsMode=off|all|tcponly
 * 2. Rate adaptation (RAA):        --scenario=2 --raaVariant=constant|minstrel|ideal
 * 3. TCP CC + station density:     --scenario=3 --tcpVariant=newreno|cubic|bbr
 *
 * TRAFFIC MODEL:
 * VoIP : G.711 UDP 100 B / 20 ms = 40 kbps, ToS=0xb8 → EDCA AC_VO
 * BG   : OnOff TCP 1000 B / 2 Mbps,         ToS=0x00 → EDCA AC_BE
 */

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — INCLUDES
// ─────────────────────────────────────────────────────────────────────────────
#include <cmath>
#include <string>

#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"

#include "ns3/ssid.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

#include "ns3/mobility-helper.h"

#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"

#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ac-ax-be-comparison_fix");

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — MAIN
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    uint32_t scenario{1};

    std::string standard{"ax"};
    std::size_t nVoip{5};
    std::size_t nBackground{3};
    bool        withBackground{true};
    uint32_t    runSeed{1};

    double      frequency{5};      // GHz
    int         channelWidth{80};  // MHz
    Time        simulationTime{"30s"};
    double      distance{10.0};    // m

    uint32_t    voipPayload{100};  // B
    double      voipInterval{0.02}; // s
    std::string bgDataRate{"2Mbps"};
    uint32_t    bgPayload{1000};   // B

    std::string rtsCtsMode{"off"};
    std::string raaVariant{"constant"};
    std::string tcpVariant{"newreno"};

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 3 — COMMAND-LINE INTERFACE
    // ─────────────────────────────────────────────────────────────────────────
    CommandLine cmd(__FILE__);

    cmd.AddValue("scenario",       "Numer scenariusza: 1 | 2 | 3",              scenario);
    cmd.AddValue("standard",       "Standard Wi-Fi: ac | ax | be",              standard);
    cmd.AddValue("nVoip",          "Liczba stacji VoIP",                        nVoip);
    cmd.AddValue("nBackground",    "Liczba stacji tła TCP",                     nBackground);
    cmd.AddValue("withBackground", "Włącz ruch tła (true/false)",               withBackground);
    cmd.AddValue("frequency",      "Pasmo w GHz (5 lub 2.4)",                   frequency);
    cmd.AddValue("channelWidth",   "Szerokość kanału MHz (20/40/80)",           channelWidth);
    cmd.AddValue("simulationTime", "Czas symulacji (np. 30s)",                  simulationTime);
    cmd.AddValue("distance",       "Odległość AP-STA w metrach",                distance);
    cmd.AddValue("runSeed",        "Ziarno RNG",                                runSeed);

    cmd.AddValue("rtsCtsMode",  "S1: tryb RTS/CTS — off | all | tcponly",       rtsCtsMode);
    cmd.AddValue("raaVariant",  "S2: algorytm RAA — constant | minstrel | ideal", raaVariant);
    cmd.AddValue("tcpVariant",  "S3: algorytm TCP CC — newreno | cubic | bbr",  tcpVariant);

    cmd.Parse(argc, argv);

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 4 — VALIDATION AND DEFAULT VALUES
    // ─────────────────────────────────────────────────────────────────────────
    if (scenario == 1)
    {
        if (rtsCtsMode != "off" && rtsCtsMode != "all" && rtsCtsMode != "tcponly")
            NS_FATAL_ERROR("S1: nieprawidłowy rtsCtsMode '" << rtsCtsMode << "'");
        raaVariant = "constant";
        tcpVariant = "newreno";
    }
    else if (scenario == 2)
    {
        if (raaVariant != "constant" && raaVariant != "minstrel" && raaVariant != "ideal")
            NS_FATAL_ERROR("S2: nieprawidłowy raaVariant '" << raaVariant << "'");
        rtsCtsMode = "off";
        tcpVariant = "newreno";
    }
    else if (scenario == 3)
    {
        if (tcpVariant != "newreno" && tcpVariant != "cubic" && tcpVariant != "bbr")
            NS_FATAL_ERROR("S3: nieprawidłowy tcpVariant '" << tcpVariant << "'");
        rtsCtsMode = "off";
        raaVariant = "constant";
    }
    else
    {
        NS_FATAL_ERROR("Nieprawidłowy numer scenariusza: " << scenario);
    }

    if (standard != "ac" && standard != "ax" && standard != "be")
        NS_FATAL_ERROR("Nieprawidłowy standard: '" << standard << "'");

    if (standard == "be")
        NS_LOG_WARN("standard=be wymaga ns-3 >= 3.40. Zastąp ax w razie błędu.");

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 5 — RNG
    // ─────────────────────────────────────────────────────────────────────────
    Config::SetGlobal("RngSeed", UintegerValue(runSeed));
    Config::SetGlobal("RngRun",  UintegerValue(runSeed));

    std::size_t nBg = withBackground ? nBackground : 0;

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 6 — RTS/CTS THRESHOLD
    // ─────────────────────────────────────────────────────────────────────────
    uint32_t rtsThreshold{2347};

    if      (rtsCtsMode == "all")     rtsThreshold = 0;
    else if (rtsCtsMode == "tcponly") rtsThreshold = 500;

    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold",
                       UintegerValue(rtsThreshold));

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 7 — TCP CONGESTION CONTROL
    // ─────────────────────────────────────────────────────────────────────────
    if      (tcpVariant == "newreno")
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    else if (tcpVariant == "cubic")
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpCubic"));
    else
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpBbr"));

    Config::SetDefault("ns3::TcpSocket::SndBufSize",  UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",  UintegerValue(1 << 21));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(bgPayload));

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 8 — NODES
    // ─────────────────────────────────────────────────────────────────────────
    NodeContainer wifiApNode;    wifiApNode.Create(1);
    NodeContainer wifiVoipNodes; wifiVoipNodes.Create(nVoip);
    NodeContainer wifiBgNodes;   wifiBgNodes.Create(nBg);

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 9 — WIFI STANDARD + RAA
    // ─────────────────────────────────────────────────────────────────────────
    WifiHelper wifi;
    WifiMacHelper mac;

    if (standard == "ax")
    {
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(800)));
    }
    else if (standard == "ac")
    {
        wifi.SetStandard(WIFI_STANDARD_80211ac);
    }
    else
    {
        wifi.SetStandard(WIFI_STANDARD_80211be);
    }

    if (raaVariant == "constant")
    {
        std::string dataMode = (standard == "ac") ? "VhtMcs7"
                             : (standard == "ax") ? "HeMcs7"
                             :                      "EhtMcs7";
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",    StringValue(dataMode),
                                     "ControlMode", StringValue("OfdmRate24Mbps"));
    }
    else if (raaVariant == "minstrel")
    {
        wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    }
    else
    {
        wifi.SetRemoteStationManager("ns3::IdealWifiManager");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 10 — CHANNEL CONFIGURATION
    // ─────────────────────────────────────────────────────────────────────────
    std::string channelStr("{0, " + std::to_string(channelWidth) + ", ");
    if      (frequency == 5)   channelStr += "BAND_5GHZ, 0}";
    else if (frequency == 2.4) channelStr += "BAND_2_4GHZ, 0}";
    else
        NS_FATAL_ERROR("Nieobsługiwane pasmo: " << frequency);

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 11 — PROPAGATION MODEL
    // ─────────────────────────────────────────────────────────────────────────
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                               "Exponent",          DoubleValue(3.0),
                               "ReferenceDistance", DoubleValue(1.0),
                               "ReferenceLoss",     DoubleValue(frequency == 5 ? 46.67 : 40.0));

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 12 — PHY AND MAC
    // ─────────────────────────────────────────────────────────────────────────
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("ChannelSettings", StringValue(channelStr));

    Ssid ssid = Ssid("voip-unified-bss");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer voipDevices = wifi.Install(phy, mac, wifiVoipNodes);
    NetDeviceContainer bgDevices   = wifi.Install(phy, mac, wifiBgNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid",               SsidValue(ssid),
                "EnableBeaconJitter", BooleanValue(false));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, wifiApNode);

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 13 — MOBILITY
    // ─────────────────────────────────────────────────────────────────────────
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();

    pos->Add(Vector(0.0, 0.0, 0.0));

    for (std::size_t i = 0; i < nVoip; i++)
    {
        double a = 2.0 * M_PI * i / nVoip;
        pos->Add(Vector(distance * std::cos(a), distance * std::sin(a), 0.0));
    }
    for (std::size_t i = 0; i < nBg; i++)
    {
        double a = 2.0 * M_PI * i / (nBg > 0 ? nBg : 1);
        pos->Add(Vector(0.7 * distance * std::cos(a), 0.7 * distance * std::sin(a), 0.0));
    }

    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);
    mobility.Install(wifiVoipNodes);
    mobility.Install(wifiBgNodes);

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 14 — INTERNET STACK AND ADDRESSING
    // ─────────────────────────────────────────────────────────────────────────
    InternetStackHelper stack;
    stack.Install(wifiApNode);
    stack.Install(wifiVoipNodes);
    stack.Install(wifiBgNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apIface    = address.Assign(apDevice);
    Ipv4InterfaceContainer voipIfaces = address.Assign(voipDevices);
    address.Assign(bgDevices);

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 15 — VOIP APPLICATIONS
    // ─────────────────────────────────────────────────────────────────────────
    uint16_t voipPortBase{5000};
    ApplicationContainer voipApps;

    for (std::size_t i = 0; i < nVoip; i++)
    {
        uint16_t ulPort = voipPortBase + static_cast<uint16_t>(2 * i);

        PacketSinkHelper ulSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), ulPort));
        auto ulSinkApp = ulSink.Install(wifiApNode.Get(0));
        ulSinkApp.Start(Seconds(0)); ulSinkApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(ulSinkApp);

        OnOffHelper ulSrc("ns3::UdpSocketFactory", InetSocketAddress(apIface.GetAddress(0), ulPort));
        ulSrc.SetConstantRate(DataRate(voipPayload * 8 * static_cast<uint64_t>(1.0 / voipInterval)), voipPayload);
        ulSrc.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        ulSrc.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ulSrc.SetAttribute("Tos", UintegerValue(0xb8));
        auto ulSrcApp = ulSrc.Install(wifiVoipNodes.Get(i));
        ulSrcApp.Start(Seconds(1)); ulSrcApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(ulSrcApp);

        uint16_t dlPort = voipPortBase + static_cast<uint16_t>(2 * i + 1);

        PacketSinkHelper dlSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort));
        auto dlSinkApp = dlSink.Install(wifiVoipNodes.Get(i));
        dlSinkApp.Start(Seconds(0)); dlSinkApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(dlSinkApp);

        OnOffHelper dlSrc("ns3::UdpSocketFactory", InetSocketAddress(voipIfaces.GetAddress(i), dlPort));
        dlSrc.SetConstantRate(DataRate(voipPayload * 8 * static_cast<uint64_t>(1.0 / voipInterval)), voipPayload);
        dlSrc.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        dlSrc.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        dlSrc.SetAttribute("Tos", UintegerValue(0xb8));
        auto dlSrcApp = dlSrc.Install(wifiApNode.Get(0));
        dlSrcApp.Start(Seconds(1)); dlSrcApp.Stop(simulationTime + Seconds(1));
        voipApps.Add(dlSrcApp);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 16 — BACKGROUND APPLICATIONS
    // ─────────────────────────────────────────────────────────────────────────
    uint16_t bgPortBase{6000};
    ApplicationContainer bgApps;

    for (std::size_t i = 0; i < nBg; i++)
    {
        uint16_t bgPort = bgPortBase + static_cast<uint16_t>(i);

        PacketSinkHelper bgSink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), bgPort));
        auto bgSinkApp = bgSink.Install(wifiApNode.Get(0));
        bgSinkApp.Start(Seconds(0)); bgSinkApp.Stop(simulationTime + Seconds(1));
        bgApps.Add(bgSinkApp);

        OnOffHelper bgSrc("ns3::TcpSocketFactory", InetSocketAddress(apIface.GetAddress(0), bgPort));
        bgSrc.SetAttribute("DataRate",   StringValue(bgDataRate));
        bgSrc.SetAttribute("PacketSize", UintegerValue(bgPayload));
        bgSrc.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        bgSrc.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        bgSrc.SetAttribute("Tos", UintegerValue(0x00));
        auto bgSrcApp = bgSrc.Install(wifiBgNodes.Get(i));
        bgSrcApp.Start(Seconds(1)); bgSrcApp.Stop(simulationTime + Seconds(1));
        bgApps.Add(bgSrcApp);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 17 — ROUTING, FLOW MONITOR, EXECUTION
    // ─────────────────────────────────────────────────────────────────────────
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor> flowMon = flowMonHelper.InstallAll();

    Simulator::Stop(simulationTime + Seconds(1));
    Simulator::Run();

    // ─────────────────────────────────────────────────────────────────────────
    // SECTION 18 — CSV OUTPUT
    // ─────────────────────────────────────────────────────────────────────────
    flowMon->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());

    std::cout << "scenario,standard,rtsCtsMode,rtsThreshold,raaVariant,tcpVariant,"
              << "nVoip,nBg,seed,"
              << "srcAddr,dstAddr,protocol,"
              << "txPkts,rxPkts,lostPkts,lossPct,"
              << "meanDelayMs,meanJitterMs,throughputKbps"
              << std::endl;

    for (const auto& kv : flowMon->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        const FlowMonitor::FlowStats& s = kv.second;

        double tx  = static_cast<double>(s.txPackets);
        double rx  = static_cast<double>(s.rxPackets);
        double los = static_cast<double>(s.lostPackets);

        double lossPct  = (tx > 0) ? los / tx * 100.0                          : 0.0;
        double delayMs  = (rx > 0) ? s.delaySum.GetSeconds()  / rx  * 1000.0   : 0.0;
        double jitterMs = (rx > 1) ? s.jitterSum.GetSeconds() / (rx-1) * 1000.0 : 0.0;
        double tputKb   = (s.rxBytes * 8.0) / simulationTime.GetSeconds() / 1000.0;

        std::cout << scenario              << ","
                  << standard              << ","
                  << rtsCtsMode            << ","
                  << rtsThreshold          << ","
                  << raaVariant            << ","
                  << tcpVariant            << ","
                  << nVoip                 << ","
                  << nBg                   << ","
                  << runSeed               << ","
                  << t.sourceAddress       << ","
                  << t.destinationAddress  << ","
                  << ((t.protocol == 6) ? "TCP" : "UDP") << ","
                  << static_cast<uint64_t>(tx)  << ","
                  << static_cast<uint64_t>(rx)  << ","
                  << static_cast<uint64_t>(los) << ","
                  << lossPct  << ","
                  << delayMs  << ","
                  << jitterMs << ","
                  << tputKb
                  << std::endl;
    }

    Simulator::Destroy();
    return 0;
}
