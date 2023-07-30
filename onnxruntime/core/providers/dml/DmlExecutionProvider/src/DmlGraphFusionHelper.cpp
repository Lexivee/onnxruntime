#pragma once

#include "DmlGraphFusionHelper.h"


namespace Dml
{
namespace DmlGraphFusionHelper
{
    Microsoft::WRL::ComPtr<ID3D12Resource>
    CreateResource(
        const ExecutionProviderImpl* provider,
        const std::byte* tensorPtr,
        size_t tensorByteSize)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

        D3D12_HEAP_PROPERTIES heapProperties = {
            D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};

        D3D12_RESOURCE_DESC resourceDesc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                            0,
                                            static_cast<uint64_t>((tensorByteSize + 3) & ~3),
                                            1,
                                            1,
                                            1,
                                            DXGI_FORMAT_UNKNOWN,
                                            {1, 0},
                                            D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};

        Microsoft::WRL::ComPtr<ID3D12Device> d3dDevice;
        ORT_THROW_IF_FAILED(provider->GetD3DDevice(d3dDevice.GetAddressOf()));

        ORT_THROW_IF_FAILED(d3dDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_GRAPHICS_PPV_ARGS(buffer.GetAddressOf())));

        ORT_THROW_IF_FAILED(provider->UploadToResource(buffer.Get(), tensorPtr, tensorByteSize));

        return buffer;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource>
    CreateCpuResource(
        const ExecutionProviderImpl* provider,
        const std::byte* tensorPtr,
        size_t tensorByteSize)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

        D3D12_HEAP_PROPERTIES heapProperties = {
            D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L0, 0, 0};

        D3D12_RESOURCE_DESC resourceDesc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                            0,
                                            static_cast<uint64_t>((tensorByteSize + 3) & ~3),
                                            1,
                                            1,
                                            1,
                                            DXGI_FORMAT_UNKNOWN,
                                            {1, 0},
                                            D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};

        Microsoft::WRL::ComPtr<ID3D12Device> d3dDevice;
        ORT_THROW_IF_FAILED(provider->GetD3DDevice(d3dDevice.GetAddressOf()));

        ORT_THROW_IF_FAILED(d3dDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_GRAPHICS_PPV_ARGS(buffer.GetAddressOf())));

        // Map the buffer and copy the data
        void* bufferData = nullptr;
        D3D12_RANGE range = {0, tensorByteSize};
        ORT_THROW_IF_FAILED(buffer->Map(0, &range, &bufferData));
        memcpy(bufferData, tensorPtr, tensorByteSize);
        buffer->Unmap(0, &range);

        return buffer;
    }

    void UnwrapTensor(
        Windows::AI::MachineLearning::Adapter::IWinmlExecutionProvider* winmlProvider,
        const onnxruntime::Tensor* tensor,
        ID3D12Resource** resource,
        uint64_t* allocId)
    {
        IUnknown* allocationUnk = static_cast<IUnknown*>(const_cast<void*>(tensor->DataRaw()));
        Microsoft::WRL::ComPtr<IUnknown> resourceUnk;
        winmlProvider->GetABIDataInterface(false, allocationUnk, &resourceUnk);

        *allocId = winmlProvider->TryGetPooledAllocationId(allocationUnk, 0);

        ORT_THROW_IF_FAILED(resourceUnk->QueryInterface(resource));
    }

    void ProcessInputData(
        const ExecutionProviderImpl* providerImpl,
        const std::vector<uint8_t>& isInputsUploadedByDmlEP,
        const DML_GRAPH_EDGE_DESC* inputEdges,
        const uint32_t inputEdgeCount,
        const gsl::span<const std::string> subGraphInputArgNames,
        const std::unordered_map<std::string, std::pair<const ONNX_NAMESPACE::TensorProto*, bool>>& initializerNameToInitializerMap,
        onnxruntime::Graph& graph,
        _Out_ std::vector<bool>& inputsUsed,
        _Inout_ std::vector<DML_BUFFER_BINDING>& initInputBindings,
        _Inout_ std::vector<ComPtr<ID3D12Resource>>& nonOwnedGraphInputsFromInitializers,
        _Inout_ std::vector<ComPtr<ID3D12Resource>>& initializeResourceRefs,
        _Inout_opt_ std::vector<std::vector<std::byte>>* inputRawData)
    {

        const uint32_t fusedNodeInputCount = gsl::narrow_cast<uint32_t>(subGraphInputArgNames.size());

        // Determine the last input which uses an initializer, so initializers can be freed incrementally
        // while processing each input in order.
        std::map<const onnx::TensorProto*, uint32_t> initializerToLastInputIndexMap;
        for (uint32_t i = 0; i < fusedNodeInputCount; i++)
        {
            auto iter = initializerNameToInitializerMap.find(subGraphInputArgNames[i]);
            if (iter != initializerNameToInitializerMap.end()) {
                initializerToLastInputIndexMap[iter->second.first] = i;
            }
        }

        // Walk through each graph edge and mark used inputs
        inputsUsed.assign(fusedNodeInputCount, false);
        for (uint32_t idx = 0; idx < inputEdgeCount; idx++) {
            auto* edge = (const DML_INPUT_GRAPH_EDGE_DESC*)(inputEdges[idx].Desc);
            inputsUsed[edge->GraphInputIndex] = true;
        }

        for (uint32_t i = 0; i < initInputBindings.size(); i++)
        {
            bool isInitializerAlreadyRemoved = false;
            // If the input isn't actually used by the graph, nothing ever needs to be bound (either for
            // initialization or execution). So just throw away the transferred initializer and skip this input.
            if (!inputsUsed[i])
            {
                auto iter = initializerNameToInitializerMap.find(subGraphInputArgNames[i]);
                if(iter != initializerNameToInitializerMap.end() && iter->second.second)
                {
                    graph.RemoveInitializedTensor(subGraphInputArgNames[i]);
                }

                if (inputRawData)
                {
                    inputRawData->push_back(std::vector<std::byte>());
                }

                continue;
            }

            // Look for the initializer among those transferred from the graph during partitioning
            auto iter = initializerNameToInitializerMap.find(subGraphInputArgNames[i]);
            if (iter != initializerNameToInitializerMap.end())
            {
                std::byte* tensorPtr = nullptr;
                size_t tensorByteSize = 0;
                std::vector<uint8_t> unpackedExternalTensor;

                std::unique_ptr<std::byte[]> unpackedTensor;

                //auto& initializer = iter->second;
                auto* initializer = iter->second.first;

                // The tensor may be stored as raw data or in typed fields.
                if (initializer->data_location() == onnx::TensorProto_DataLocation_EXTERNAL)
                {
                    THROW_IF_NOT_OK(onnxruntime::utils::UnpackInitializerData(*initializer, graph.ModelPath(), unpackedExternalTensor));
                    tensorPtr = reinterpret_cast<std::byte*>(unpackedExternalTensor.data());
                    tensorByteSize = unpackedExternalTensor.size();
                }
                else if (initializer->has_raw_data())
                {
                    tensorPtr = (std::byte*)(initializer->raw_data().c_str());
                    tensorByteSize = initializer->raw_data().size();
                }
                else
                {
                    std::tie(unpackedTensor, tensorByteSize) = Windows::AI::MachineLearning::Adapter::UnpackTensor(*initializer, graph.ModelPath());
                    tensorPtr = unpackedTensor.get();

                    // Free the initializer if this is the last usage of it.
                    if (initializerToLastInputIndexMap[initializer] == i)
                    {
                        if (iter->second.second)
                        {
                            graph.RemoveInitializedTensor(subGraphInputArgNames[i]);
                            isInitializerAlreadyRemoved = true;
                        }
                    }
                }

                // Tensor sizes in DML must be a multiple of 4 bytes large.
                tensorByteSize = AlignToPow2<size_t>(tensorByteSize, 4);

                if (inputRawData)
                {
                    inputRawData->push_back(std::vector<std::byte>(tensorPtr, tensorPtr + tensorByteSize));
                }

                if (!isInputsUploadedByDmlEP[i])
                {
                    // Store the resource to use during execution
                    ComPtr<ID3D12Resource> defaultBuffer = CreateResource(providerImpl, tensorPtr, tensorByteSize);
                    nonOwnedGraphInputsFromInitializers[i] = defaultBuffer;
                    initializeResourceRefs.push_back(std::move(defaultBuffer));
                }
                else
                {
                    ComPtr<ID3D12Resource> initializeInputBuffer;

                    // D3D_FEATURE_LEVEL_1_0_CORE doesn't support Custom heaps
                    if (providerImpl->IsMcdmDevice())
                    {
                        initializeInputBuffer = CreateResource(providerImpl, tensorPtr, tensorByteSize);
                    }
                    else
                    {
                        initializeInputBuffer = CreateCpuResource(providerImpl, tensorPtr, tensorByteSize);
                    }

                    // Set the binding for operator initialization to the buffer
                    initInputBindings[i].Buffer = initializeInputBuffer.Get();
                    initInputBindings[i].SizeInBytes = tensorByteSize;
                    initializeResourceRefs.push_back(std::move(initializeInputBuffer));
                }

                // Free the initializer if this is the last usage of it.
                if (!isInitializerAlreadyRemoved && initializerToLastInputIndexMap[initializer] == i)
                {
                    if (iter->second.second)
                    {
                        graph.RemoveInitializedTensor(subGraphInputArgNames[i]);
                    }
                }
            }
            else if (inputRawData)
            {
                inputRawData->push_back(std::vector<std::byte>());
            }
        }
    }

    std::unordered_map<const onnx::TensorProto*, std::vector<uint32_t>>
    GetInitializerToPartitionMap(
        const onnxruntime::GraphViewer& graph,
        gsl::span<std::unique_ptr<GraphPartition>> partitions
    )
    {
        std::unordered_map<const onnx::TensorProto*, std::vector<uint32_t>> initializerPartitionMap;
        for (uint32_t partitionIndex = 0; partitionIndex < gsl::narrow_cast<uint32_t>(partitions.size()); ++partitionIndex)
        {
            auto& partition = partitions[partitionIndex];

            // Skip partitions which have been merged into other partitions
            if (partition->GetRootMergedPartition() != partition.get())
            {
                continue;
            }

            for (const std::string& input : partition->GetInputs())
            {
                const onnx::TensorProto* tensor = nullptr;
                if (graph.GetInitializedTensor(input, tensor))
                {
                    initializerPartitionMap[tensor].push_back(partitionIndex);
                }
            }
        }

        return initializerPartitionMap;
    }

    template <size_t ALLOCATER_SIZE>
    void ConvertGraphDesc(
        const Dml::GraphDescBuilder::GraphDesc& graphDesc,
        const onnxruntime::IndexedSubGraph& indexedSubGraph,
        _Out_ DML_GRAPH_DESC& dmlGraphDesc,
        const uint32_t inputCount,
        const uint32_t outputCount,
        IDMLDevice* device,
        const std::unordered_map<uint32_t, uint32_t>& constantEdgeIdxToSubgraphInputArgIdxMap,
        StackAllocator<ALLOCATER_SIZE>& allocator,
        _Inout_ std::vector<DML_GRAPH_NODE_DESC>& dmlGraphNodes,
        _Inout_ std::vector<DML_GRAPH_EDGE_DESC>& dmlInputEdges,
        _Inout_ std::vector<DML_GRAPH_EDGE_DESC>& dmlOutputEdges,
        _Inout_ std::vector<DML_GRAPH_EDGE_DESC>& dmlIntermediateEdges,
        _Inout_ std::vector<ComPtr<IDMLOperator>>& dmlOperators)
    {
        std::unordered_map<uint32_t, uint32_t> oldNodeIdxToNewNodeIdxMap;
        for (uint32_t idx = 0; idx < static_cast<uint32_t>(graphDesc.Nodes.size()); idx++)
        {
            const DmlSerializedGraphNode& node = graphDesc.Nodes[idx];
            if (std::holds_alternative<AbstractOperatorDesc>(node.Desc))
            {
                oldNodeIdxToNewNodeIdxMap[idx] = static_cast<uint32_t>(dmlOperators.size());
                DML_OPERATOR_DESC dmlDesc = SchemaHelpers::ConvertOperatorDesc(std::get<AbstractOperatorDesc>(node.Desc), &allocator);
                ComPtr<IDMLOperator> op;
                ORT_THROW_IF_FAILED(device->CreateOperator(&dmlDesc, IID_PPV_ARGS(&op)));
                dmlOperators.push_back(op);
                DML_OPERATOR_GRAPH_NODE_DESC* dmlOperatorGraphNode = allocator.Allocate<DML_OPERATOR_GRAPH_NODE_DESC>();
                dmlOperatorGraphNode->Name = node.Name.data();
                dmlOperatorGraphNode->Operator = op.Get();
                dmlGraphNodes.push_back(DML_GRAPH_NODE_DESC{DML_GRAPH_NODE_TYPE_OPERATOR, dmlOperatorGraphNode});
            }
        }

        for (size_t i = 0; i < graphDesc.InputEdges.size(); ++i)
        {
            DML_INPUT_GRAPH_EDGE_DESC* edge = allocator.Allocate<DML_INPUT_GRAPH_EDGE_DESC>();
            edge->GraphInputIndex = graphDesc.InputEdges[i].GraphInputIndex;  // DmlSerializedGraph will use the same onnx arg index
            edge->ToNodeIndex = oldNodeIdxToNewNodeIdxMap[graphDesc.InputEdges[i].ToNodeIndex];
            edge->ToNodeInputIndex = graphDesc.InputEdges[i].ToNodeInputIndex;
            edge->Name = graphDesc.InputEdges[i].Name.data();
            dmlInputEdges.push_back(DML_GRAPH_EDGE_DESC{DML_GRAPH_EDGE_TYPE_INPUT, edge});
        }

        for (size_t i = 0; i < graphDesc.OutputEdges.size(); ++i)
        {
            DML_OUTPUT_GRAPH_EDGE_DESC* edge = allocator.Allocate<DML_OUTPUT_GRAPH_EDGE_DESC>();
            edge->GraphOutputIndex = graphDesc.OutputEdges[i].GraphOutputIndex;
            edge->FromNodeIndex = oldNodeIdxToNewNodeIdxMap[graphDesc.OutputEdges[i].FromNodeIndex];
            edge->FromNodeOutputIndex = graphDesc.OutputEdges[i].FromNodeOutputIndex;
            edge->Name = graphDesc.OutputEdges[i].Name.data();
            dmlOutputEdges[i] = DML_GRAPH_EDGE_DESC{DML_GRAPH_EDGE_TYPE_OUTPUT, edge};
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(graphDesc.IntermediateEdges.size()); ++i)
        {
            DmlSerializedGraphNodeDescVariant descVariant = graphDesc.Nodes[graphDesc.IntermediateEdges[i].FromNodeIndex].Desc;
            bool isConstantEdge = std::holds_alternative<DmlSerializedGraphNodeConstantVariant>(descVariant);
            if (isConstantEdge)
            {
                DML_INPUT_GRAPH_EDGE_DESC* edge = allocator.Allocate<DML_INPUT_GRAPH_EDGE_DESC>();
                //edge->GraphInputIndex = static_cast<uint32_t>(dmlInputEdges.size());
                edge->GraphInputIndex = constantEdgeIdxToSubgraphInputArgIdxMap.at(i);
                edge->ToNodeIndex = oldNodeIdxToNewNodeIdxMap[graphDesc.IntermediateEdges[i].ToNodeIndex];
                edge->ToNodeInputIndex = graphDesc.IntermediateEdges[i].ToNodeInputIndex;
                edge->Name = graphDesc.IntermediateEdges[i].Name.data();
                dmlInputEdges.push_back({DML_GRAPH_EDGE_TYPE_INPUT, edge});

                // todo: handle ConstantData
            }
            else
            {   
                DML_INTERMEDIATE_GRAPH_EDGE_DESC* edge = allocator.Allocate<DML_INTERMEDIATE_GRAPH_EDGE_DESC>();
                edge->FromNodeIndex = oldNodeIdxToNewNodeIdxMap[graphDesc.IntermediateEdges[i].FromNodeIndex];
                edge->FromNodeOutputIndex = graphDesc.IntermediateEdges[i].FromNodeOutputIndex;
                edge->ToNodeIndex = oldNodeIdxToNewNodeIdxMap[graphDesc.IntermediateEdges[i].ToNodeIndex];
                edge->ToNodeInputIndex = graphDesc.IntermediateEdges[i].ToNodeInputIndex;
                edge->Name = graphDesc.IntermediateEdges[i].Name.data();
                dmlIntermediateEdges.push_back(DML_GRAPH_EDGE_DESC{DML_GRAPH_EDGE_TYPE_INTERMEDIATE, edge});
            }
        }

        dmlGraphDesc.InputCount = inputCount;
        dmlGraphDesc.OutputCount = outputCount;
        dmlGraphDesc.NodeCount = gsl::narrow_cast<uint32_t>(dmlGraphNodes.size());
        dmlGraphDesc.Nodes = dmlGraphNodes.data();
        dmlGraphDesc.InputEdgeCount = gsl::narrow_cast<uint32_t>(dmlInputEdges.size());
        dmlGraphDesc.InputEdges = dmlInputEdges.data();
        dmlGraphDesc.OutputEdgeCount = gsl::narrow_cast<uint32_t>(dmlOutputEdges.size());
        dmlGraphDesc.OutputEdges = dmlOutputEdges.data();
        dmlGraphDesc.IntermediateEdgeCount = gsl::narrow_cast<uint32_t>(dmlIntermediateEdges.size());
        dmlGraphDesc.IntermediateEdges = dmlIntermediateEdges.data();
    }

    void CreateIDmlCompiledOperatorAndRegisterKernel(
        onnxruntime::Graph& graph,
        const onnxruntime::IndexedSubGraph& indexedSubGraph,
        const onnxruntime::Node& fusedNode,
        const std::unordered_map<std::string, GraphNodeProperties>& partitionNodePropsMap,
        const std::unordered_map<std::string, std::pair<const ONNX_NAMESPACE::TensorProto*, bool>>& initializerNameToInitializerMap,
        const ExecutionProviderImpl* providerImpl,
        onnxruntime::KernelRegistry* registryForPartitionKernels)
    {
        // convert partitionONNXGraph into DML EP GraphDesc
        const uint32_t fusedNodeInputCount = gsl::narrow_cast<uint32_t>(indexedSubGraph.GetMetaDef()->inputs.size());
        const uint32_t fusedNodeOutputCount = gsl::narrow_cast<uint32_t>(indexedSubGraph.GetMetaDef()->outputs.size());

        std::vector<uint8_t> isInputsUploadedByDmlEP(fusedNodeInputCount, false);
        for (uint32_t index = 0; index < fusedNodeInputCount; ++index)
        {
            auto iter = initializerNameToInitializerMap.find(indexedSubGraph.GetMetaDef()->inputs[index]);
            if (iter != initializerNameToInitializerMap.end())
            {
                isInputsUploadedByDmlEP[index] = true;
            }
        }

        ComPtr<IDMLDevice> device;
        ORT_THROW_IF_FAILED(providerImpl->GetDmlDevice(device.GetAddressOf()));
        const DmlSerializedGraphDesc foo = {};
        // This map will be used to transfer the initializer to D3D12 system heap memory.
        // 'serializedDmlGraphDesc' will have constant input as intermediate edges, that's why
        // we need a mapping between intermediateEdgeIndex and indexedSubGraph's (a given partition)
        // input arg index.
        //   For ex: Let's say intermediate edge index = idx, then
        //           indexedSubGraphInputArgIdx = constantEdgeIdxToSubgraphInputArgIdxMap[idx];
        //           corresponding constant tensor = initializerNameToInitializerMap[indexedSubGraph.GetMetaDef()->inputs[indexedSubGraphInputArgIdx]]
        // We are using intermediate edge index as a key because same constant tensor can be used by
        // multiple nodes.
        std::unordered_map<uint32_t, uint32_t> constantEdgeIdxToSubgraphInputArgIdxMap;
        GraphDescBuilder::GraphDesc serializedDmlGraphDesc = GraphDescBuilder::BuildDmlGraphDesc(
            isInputsUploadedByDmlEP.data(),
            isInputsUploadedByDmlEP.size(),
            initializerNameToInitializerMap,
            graph,
            indexedSubGraph,
            partitionNodePropsMap,
            device.Get(),
            providerImpl,
            constantEdgeIdxToSubgraphInputArgIdxMap);

        auto buffer = SerializeDmlGraph(serializedDmlGraphDesc);
        //auto deserializedDmlGraphDesc = DeserializeDmlGraph(buffer.data());

        // convert DML EP GraphDesc into DML_GRAPH_DESC and create IDMLCompiledOperator
        StackAllocator<1024> allocator; // Used for converting DmlSerializedGraphDesc to DML_GRAPH_DESC
        DML_GRAPH_DESC dmlGraphDesc = {};
        std::vector<ComPtr<IDMLOperator>> dmlOperators;
        std::vector<DML_GRAPH_NODE_DESC> dmlGraphNodes;
        std::vector<DML_GRAPH_EDGE_DESC> dmlInputEdges;
        std::vector<DML_GRAPH_EDGE_DESC> dmlOutputEdges(serializedDmlGraphDesc.OutputEdges.size());
        std::vector<DML_GRAPH_EDGE_DESC> dmlIntermediateEdges;
        ConvertGraphDesc(
            serializedDmlGraphDesc,
            indexedSubGraph,
            dmlGraphDesc,
            fusedNodeInputCount,
            fusedNodeOutputCount,
            device.Get(),
            constantEdgeIdxToSubgraphInputArgIdxMap,
            allocator,
            dmlGraphNodes,
            dmlInputEdges,
            dmlOutputEdges,
            dmlIntermediateEdges,
            dmlOperators);

        DML_EXECUTION_FLAGS executionFlags = DML_EXECUTION_FLAG_NONE;
        if (serializedDmlGraphDesc.reuseCommandList)
        {
            executionFlags |= DML_EXECUTION_FLAG_DESCRIPTORS_VOLATILE;
        }

        // Query DML execution provider to see if metacommands is enabled
        if (!providerImpl->MetacommandsEnabled())
        {
            executionFlags |= DML_EXECUTION_FLAG_DISABLE_META_COMMANDS;
        }

        ComPtr<IDMLDevice1> device1;
        ORT_THROW_IF_FAILED(device.As(&device1));
        ComPtr<IDMLCompiledOperator> compiledExecutionPlanOperator;
        ORT_THROW_IF_FAILED(device1->CompileGraph(
            &dmlGraphDesc,
            executionFlags,
            IID_PPV_ARGS(&compiledExecutionPlanOperator)));

        // Populate input bindings for operator initialization
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> initializeResourceRefs; // For lifetime control
        std::vector<DML_BUFFER_BINDING> initInputBindings(fusedNodeInputCount);
        std::vector<ComPtr<ID3D12Resource>> nonOwnedGraphInputsFromInitializers(fusedNodeInputCount);

        std::vector<bool> inputsUsed;
        ProcessInputData(
            providerImpl,
            isInputsUploadedByDmlEP,
            dmlGraphDesc.InputEdges,
            dmlGraphDesc.InputEdgeCount,
            indexedSubGraph.GetMetaDef()->inputs,
            initializerNameToInitializerMap,
            graph,
            inputsUsed,
            initInputBindings,
            nonOwnedGraphInputsFromInitializers,
            initializeResourceRefs,
            nullptr);

        // lamda captures for the kernel registration
        Windows::AI::MachineLearning::Adapter::EdgeShapes outputShapes;
        ORT_THROW_HR_IF(E_UNEXPECTED, !TryGetStaticOutputShapes(fusedNode, outputShapes));
        bool resuableCommandList = serializedDmlGraphDesc.reuseCommandList;
        auto fused_kernel_func = [compiledExecutionPlanOperator,
                                  outputShapes,
                                  resuableCommandList,
                                  nonOwnedGraphInputsFromInitializers,
                                  initializeResourceRefs,
                                  initInputBindings,
                                  isInputsUploadedByDmlEP,
                                  inputsUsed]
                    (onnxruntime::FuncManager& func_mgr, const onnxruntime::OpKernelInfo& info, std::unique_ptr<onnxruntime::OpKernel>& out) mutable ->onnxruntime::Status
        {
            out.reset(CreateFusedGraphKernel(info,
                                             compiledExecutionPlanOperator,
                                             outputShapes,
                                             resuableCommandList,
                                             nonOwnedGraphInputsFromInitializers,
                                             initializeResourceRefs,
                                             initInputBindings,
                                             isInputsUploadedByDmlEP,
                                             inputsUsed));
            return Status::OK();
        };

        // build the kernel definition on the fly, and register it to the fused_kernel_regisitry.
        onnxruntime::KernelDefBuilder builder;
        builder.SetName(indexedSubGraph.GetMetaDef()->name)
            .SetDomain(indexedSubGraph.GetMetaDef()->domain)
            .SinceVersion(indexedSubGraph.GetMetaDef()->since_version)
            .Provider(onnxruntime::kDmlExecutionProvider);
        ORT_THROW_IF_ERROR(registryForPartitionKernels->Register(builder, fused_kernel_func));
    }

    void FusePartitionAndRegisterKernel(
        GraphPartition* partition,
        uint32_t partitionIndex,
        onnxruntime::Graph& graph,
        std::unordered_map<const onnxruntime::Node*, GraphNodeProperties>& graphNodePropertyMap,
        onnxruntime::KernelRegistry* registryForPartitionKernels,
        const std::string& partitionKernelPrefix,
        const std::unordered_map<std::string, std::pair<const ONNX_NAMESPACE::TensorProto*, bool>>& initializerNameToInitializerMap,
        const ExecutionProviderImpl* providerImpl)
    {
        assert(partition->IsDmlGraphPartition());

        onnxruntime::IndexedSubGraph indexedSubGraph;
        // Create a definition for the node.  The name must be unique.
        auto def = std::make_unique<onnxruntime::IndexedSubGraph::MetaDef>();
        def->name = DmlGraphFusionTransformer::DML_GRAPH_FUSION_NODE_NAME_PREFIX + partitionKernelPrefix + std::to_string(partitionIndex);
        def->domain = DmlGraphFusionTransformer::DML_GRAPH_FUSION_NODE_DOMAIN;
        def->since_version = 1;
        def->inputs.insert(def->inputs.begin(), partition->GetInputs().begin(), partition->GetInputs().end());
        def->outputs.insert(def->outputs.begin(), partition->GetOutputs().begin(), partition->GetOutputs().end());

        indexedSubGraph.SetMetaDef(std::move(def));
        indexedSubGraph.nodes = std::move(partition->GetNodeIndices());
        auto& fusedNode = graph.BeginFuseSubGraph(indexedSubGraph, indexedSubGraph.GetMetaDef()->name);
        fusedNode.SetExecutionProviderType(onnxruntime::kDmlExecutionProvider);

        // Populate properties which will be passed to OpKernel for this graph via the function below
        std::unordered_map<std::string, GraphNodeProperties> partitionNodePropsMap;
        for (auto nodeIndex : indexedSubGraph.nodes)
        {
            const onnxruntime::Node* node = graph.GetNode(nodeIndex);

#ifdef PRINT_PARTITON_INFO
            printf("Partition %u\t%s\n", partitionIndex, GraphDescBuilder::GetUniqueNodeName(*node).c_str());
#endif
            partitionNodePropsMap.insert(std::make_pair(
                GraphDescBuilder::GetUniqueNodeName(*node), std::move(graphNodePropertyMap[node])));
        }

#ifdef PRINT_PARTITON_INFO
        printf("\n");
#endif
        CreateIDmlCompiledOperatorAndRegisterKernel(
            graph,
            indexedSubGraph,
            fusedNode,
            partitionNodePropsMap,
            initializerNameToInitializerMap,
            providerImpl,
            registryForPartitionKernels);
        graph.FinalizeFuseSubGraph(indexedSubGraph, fusedNode);
    }
}
}
