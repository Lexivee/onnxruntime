echo "HIP_VISIBLE_DEVICES=$HIP_VISIBLE_DEVICES" 
echo "PythonManylinuxDir=$PythonManylinuxDir"

$PythonManylinuxDir/bin/python3 -m pip install /build/Release/dist/*.whl 
/onnxruntime_src/tools/doc/builddoc.sh $PythonManylinuxDir/bin/ /onnxruntime_src /build Release  
