#!/bin/bash/

while getopts d:o:m:w:e: parameter
do case "${parameter}"
in
d) PERF_DIR=${OPTARG};;
o) OPTION=${OPTARG};;
m) MODEL_PATH=${OPTARG};;
w) WORKSPACE=${OPTARG};;
e) EP_LIST=${OPTARG};;
esac
done 

# add ep list
RUN_EPS=""
if [ ! -z "$EP_LIST" ]
then 
    RUN_EPS="--ep_list $EP_LIST"
fi

# change dir if docker
if [ ! -z $PERF_DIR ]
then 
    echo 'changing to '$PERF_DIR
    cd $PERF_DIR 
fi 

# files to download info
FLOAT_16="float16.py"
FLOAT_16_LINK="https://raw.githubusercontent.com/microsoft/onnxconverter-common/master/onnxconverter_common/float16.py"

wget --no-check-certificate -c $FLOAT_16_LINK 
python3 benchmark_wrapper.py -r validate -m $MODEL_PATH -o result/$OPTION -w $WORKSPACE $RUN_EPS
python3 benchmark_wrapper.py -r benchmark -t 1200 -m $MODEL_PATH -o result/$OPTION -w $WORKSPACE $RUN_EPS
