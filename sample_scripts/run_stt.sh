#!/bin/bash

# put your gem5 STT path here
STT_PATH=

# put your executable path here
EXE_PATH=

# gem5 output path
OUT_DIR=$STT_PATH/output

# gem5 configuration file
CONFIG_FILE=$GSTT_PATH/configs/example/se.py


$GEM5_PATH/build/X86_MESI_Two_Level/gem5.opt --outdir=$OUT_DIR \
    $CONFIG_FILE \
    --num-cpus=1 --mem-size=4GB \
    --caches --l2cache --cpu-type=DerivO3CPU \
    --threat_model=Spectre --needsTSO=1 --STT=1 --implicit_channel=1 \
    -c $EXE_PATH
