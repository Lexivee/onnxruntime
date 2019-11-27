#!/bin/bash
: '
Use this job template to submit job.

{
	"version": "2019-11-09",
	"metadata": {
		"name": "single_node_full_benchmarking_1109",
		"cluster": "rr3",
		"vc": "phillytest"
	},
	"resources": {
		"workers": {
			"type": "skuResource",
			"sku": "G16",
			"count": 1,
			"image": "phillyregistry.azurecr.io/philly/jobs/custom/onnxruntime:v1",
			"commandLine": "$PHILLY_DATA_DIRECTORY/$PHILLY_VC/pengwa/profile/single_node_profile.sh",
			"constraints": [
				{
					"type": "uniqueConstraint",
					"tag": "connectivityDomain"
				}
			],
			"containerArgs": {
				"shmSize": "4G"
			}
		}
	}
}
'


if [ $PHILLY_CONTAINER_INDEX -ne 0 ]
then
  echo "Not first container, skip by intention"
  sleep infinity
  exit 0
fi

echo "########## Get GPU Clock Rate/System Load Start ###############"
nvidia-smi  -q -i 0 -d CLOCK

nvidia-smi

echo "########## Get GPU Clock Rate/System Load End ###############"

if [ $PHILLY_VC == "msrhyperscl" ]; then
  export TEST_MODE=True
fi
#export TEST_MODE=True
######################## PT #############################
uptime
export PT_SCRIPT_PATH=$PHILLY_DATA_DIRECTORY/$PHILLY_VC/pengwa/profile/scripts-pt/
bash $PT_SCRIPT_PATH/single_node_profile-pt.sh
nvidia-smi  -q -i 0 -d CLOCK
######################## ORT #############################
uptime
export ORT_SCRIPT_PATH=$PHILLY_DATA_DIRECTORY/$PHILLY_VC/pengwa/profile/scripts-ort/
export OMP_NUM_THREADS=1
commitid="13954366" 
bash $ORT_SCRIPT_PATH/single_node_profile.sh $commitid " --use_nccl=True --use_nccl_tensor_fusion=True"
nvidia-smi  -q -i 0 -d CLOCK

uptime

echo "##################### Real Training"
export CUSTOM_PARAMS_STRING=" --use_nccl=True --use_nccl_tensor_fusion=True"
bash $ORT_SCRIPT_PATH"profile_real_training.sh" "nonfixed" "philly"
unset CUSTOM_PARAMS_STRING 

bash $PT_SCRIPT_PATH"profile_real_training-pt.sh" "nonfixed"
exit 0
