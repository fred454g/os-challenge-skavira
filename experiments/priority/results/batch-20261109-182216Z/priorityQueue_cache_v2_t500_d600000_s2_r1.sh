#!/bin/sh
cd /home/vagrant/os-challenge-common || exit 1
/usr/bin/time -p -o /home/vagrant/os-challenge-common/experiments/priority/results/priorityQueue_cache_v2_t500_d600000_s2_r1.time env TOTAL=500 START=0 DIFFICULTY=30000000 REP_PROB_PERCENT=20 DELAY_US=600000 PRIO_LAMBDA=1.5 EXP=priority VARIANT=priorityQueue_cache_v2 SEED=2 RUN=1 sh /home/vagrant/os-challenge-common/experiments/priority/results/batch-20261109-182216Z/run-experiment.sh
result=$?
printf '%s\n' "$result" > /home/vagrant/os-challenge-common/experiments/priority/results/batch-20261109-182216Z/priorityQueue_cache_v2_t500_d600000_s2_r1.exit
exit "$result"
