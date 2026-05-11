#!/bin/bash

> ~/results_bg.csv # Clear file

for STD in ac ax; do
    for N in 2 4 6 8 10 12 15 20; do
   
        ./ns3 run "scratch/ac-vs-ax \
        --standard=$STD \
        --nVoip=$N \
        --nBackground=3 \
        --withBackground=true \
        --runSeed=1" >> ~/results_bg.csv
  done
done
