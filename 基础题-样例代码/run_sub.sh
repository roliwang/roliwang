#!/bin/bash
#CBATCH -A hpcgame_nuaa
#CBATCH -p hygon7495 
#CBATCH --qos hpc
#CBATCH -J lifegame
#CBATCH -N 1
#CBATCH --ntasks-per-node 1
#CBATCH -c 8
#CBATCH --time 00:30:00
#CBATCH --chdir /fs0/home/用户名/HPCGAME/01（请自行创建）
#CBATCH --output job.%j.out
#CBATCH --mem 47360M
#CBATCH --export ALL
#CBATCH --get-user-env

source /fs0/software/modules-5.6.0/tools/modules/init/profile.sh
#gcc编译
module load gcc/13.1.0
make clean > /dev/null 2>&1
make life 2>&1
./life  input/$inp > serial_${inp}.txt 2>/dev/null