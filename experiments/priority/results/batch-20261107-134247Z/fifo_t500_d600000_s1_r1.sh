#!/bin/sh
cd /home/vagrant/os-challenge-common || exit 1
/usr/bin/time -p -o /home/vagrant/os-challenge-common/experiments/priority/results/fifo_t500_d600000_s1_r1.time env TOTAL=500 START=0 DIFFICULTY=30000000 REP_PROB_PERCENT=20 DELAY_US=600000 PRIO_LAMBDA=1.5 EXP=priority VARIANT=fifo SEED=1 RUN=1 sh /home/vagrant/os-challenge-common/experiments/priority/results/batch-20261107-134247Z/run-experiment.sh
result=$?
printf '%s\n' "$result" > /home/vagrant/os-challenge-common/experiments/priority/results/batch-20261107-134247Z/fifo_t500_d600000_s1_r1.exit
exit "$result"
