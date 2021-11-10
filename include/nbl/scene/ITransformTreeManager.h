// Copyright (C) 2019 - DevSH Graphics Programming Sp. z O.O.
// For conditions of distribution and use, see copyright notice in nabla.h
// See the original file in irrlicht source for authors

#ifndef __NBL_SCENE_I_TREE_TRANSFORM_MANAGER_H_INCLUDED__
#define __NBL_SCENE_I_TREE_TRANSFORM_MANAGER_H_INCLUDED__

#include "nbl/core/declarations.h"
#include "nbl/video/declarations.h"

#include "nbl/core/definitions.h"

#include "nbl/scene/ITransformTree.h"

namespace nbl::scene
{

//
#define uint uint32_t
#define int int32_t
#define uvec4 core::vectorSIMDu32
#include "nbl/builtin/glsl/transform_tree/relative_transform_modification.glsl"
#include "nbl/builtin/glsl/transform_tree/modification_request_range.glsl"
#undef uvec4
#undef int
#undef uint

class ITransformTreeManager : public virtual core::IReferenceCounted
{
	public:
		struct RelativeTransformModificationRequest : nbl_glsl_transform_tree_relative_transform_modification_t
		{
			public:
				enum E_TYPE : uint32_t
				{
					ET_OVERWRITE=_NBL_BUILTIN_TRANSFORM_TREE_RELATIVE_TRANSFORM_MODIFICATION_T_E_TYPE_OVERWRITE_, // exchange the value, `This(vertex)`
					ET_CONCATENATE_AFTER=_NBL_BUILTIN_TRANSFORM_TREE_RELATIVE_TRANSFORM_MODIFICATION_T_E_TYPE_CONCATENATE_AFTER_, // apply transform after, `This(Previous(vertex))`
					ET_CONCATENATE_BEFORE=_NBL_BUILTIN_TRANSFORM_TREE_RELATIVE_TRANSFORM_MODIFICATION_T_E_TYPE_CONCATENATE_BEFORE_, // apply transform before, `Previous(This(vertex))`
					ET_WEIGHTED_ACCUMULATE=_NBL_BUILTIN_TRANSFORM_TREE_RELATIVE_TRANSFORM_MODIFICATION_T_E_TYPE_WEIGHTED_ACCUMULATE_, // add to existing value, `(Previous+This)(vertex)`
					ET_COUNT=_NBL_BUILTIN_TRANSFORM_TREE_RELATIVE_TRANSFORM_MODIFICATION_T_E_TYPE_COUNT_
				};
				RelativeTransformModificationRequest() = default;
				RelativeTransformModificationRequest(const E_TYPE type, const core::matrix3x4SIMD& _preweightedModification)
				{
					constexpr uint32_t log2ET_COUNT = 2u;
					static_assert(ET_COUNT<=(0x1u<<log2ET_COUNT),"Need to rewrite the type encoding routine!");
				
					//
					*reinterpret_cast<core::matrix3x4SIMD*>(data) = _preweightedModification;

					// stuff the bits into x and z components of scale (without a rotation) 
					// clear then bitwise-or
					data[0][0] &= 0xfffffffeu;
					data[0][0] |= type&0x1u;
					data[2][2] &= 0xfffffffeu;
					data[2][2] |= (type>>1u)&0x1u;
				}
				RelativeTransformModificationRequest(const E_TYPE type, const core::matrix3x4SIMD& _modification, const float weight) : RelativeTransformModificationRequest(type,_modification*weight) {}

				inline E_TYPE getType() const
				{
					return static_cast<E_TYPE>(nbl_glsl_transform_tree_relative_transform_modification_t_getType(*this));
				}
		};

		// creation
        static inline core::smart_refctd_ptr<ITransformTreeManager> create(video::IUtilities* utils, video::IGPUQueue* uploadQueue)
        {
			auto device = utils->getLogicalDevice();
			auto system = device->getPhysicalDevice()->getSystem();
			auto createShader = [&system,&device](auto uniqueString, asset::IShader::E_SHADER_STAGE type=asset::IShader::ESS_COMPUTE) -> core::smart_refctd_ptr<video::IGPUSpecializedShader>
			{
				auto glslFile = system->loadBuiltinData<decltype(uniqueString)>();
				core::smart_refctd_ptr<asset::ICPUBuffer> glsl;
				{
					glsl = core::make_smart_refctd_ptr<asset::ICPUBuffer>(glslFile->getSize());
					memcpy(glsl->getPointer(), glslFile->getMappedPointer(), glsl->getSize());
				}
				auto shader = device->createGPUShader(core::make_smart_refctd_ptr<asset::ICPUShader>(core::smart_refctd_ptr(glsl),asset::IShader::buffer_contains_glsl_t{}, type, "????"));
				return device->createGPUSpecializedShader(shader.get(),{nullptr,nullptr,"main"});
			};

			auto updateRelativeSpec = createShader(NBL_CORE_UNIQUE_STRING_LITERAL_TYPE("nbl/builtin/glsl/transform_tree/relative_transform_update.comp")());
			auto recomputeGlobalSpec = createShader(NBL_CORE_UNIQUE_STRING_LITERAL_TYPE("nbl/builtin/glsl/transform_tree/global_transform_update.comp")());
			// TODO: audit source code
			auto debugDrawVertexSpec = createShader(NBL_CORE_UNIQUE_STRING_LITERAL_TYPE("nbl/builtin/glsl/transform_tree/debug.vert")(),asset::IShader::ESS_VERTEX);
			auto debugDrawFragmentSpec = createShader(NBL_CORE_UNIQUE_STRING_LITERAL_TYPE("nbl/builtin/material/debug/vertex_normal/specialized_shader.frag")(),asset::IShader::ESS_FRAGMENT);
			if (!updateRelativeSpec || !recomputeGlobalSpec || !debugDrawVertexSpec || !debugDrawFragmentSpec)
				return nullptr;

			const auto& limits = device->getPhysicalDevice()->getLimits();
			core::vector<uint8_t> tmp(getDefaultValueBufferOffset(limits,~0u));
			uint8_t* fillData = tmp.data();
			{
				*reinterpret_cast<ITransformTree::parent_t*>(fillData+getDefaultValueBufferOffset(limits,ITransformTree::parent_prop_ix)) = ITransformTree::invalid_node;
				*reinterpret_cast<ITransformTree::relative_transform_t*>(fillData+getDefaultValueBufferOffset(limits,ITransformTree::relative_transform_prop_ix)) = core::matrix3x4SIMD();
				*reinterpret_cast<ITransformTree::modified_stamp_t*>(fillData+getDefaultValueBufferOffset(limits,ITransformTree::modified_stamp_prop_ix)) = ITransformTree::initial_modified_timestamp;
				*reinterpret_cast<ITransformTree::recomputed_stamp_t*>(fillData+getDefaultValueBufferOffset(limits,ITransformTree::recomputed_stamp_prop_ix)) = ITransformTree::initial_recomputed_timestamp;
			}
			auto defaultFillValues = utils->createFilledDeviceLocalGPUBufferOnDedMem(uploadQueue,tmp.size(),fillData);
			defaultFillValues->setObjectDebugName("ITransformTreeManager::m_defaultFillValues");
			tmp.resize(sizeof(uint16_t)*IndexCount);
			uint16_t* debugIndices = reinterpret_cast<uint16_t*>(tmp.data());
			{
				debugIndices[0] = 0b000;
				debugIndices[1] = 0b001;
				debugIndices[2] = 0b001;
				debugIndices[3] = 0b010;
				debugIndices[4] = 0b010;
				debugIndices[5] = 0b011;
				debugIndices[6] = 0b011;
				debugIndices[7] = 0b000;
				debugIndices[8] = 0b000;
				debugIndices[9] = 0b100;
				debugIndices[10] = 0b001;
				debugIndices[11] = 0b101;
				debugIndices[12] = 0b010;
				debugIndices[13] = 0b110;
				debugIndices[14] = 0b011;
				debugIndices[15] = 0b111;
				debugIndices[16] = 0b100;
				debugIndices[17] = 0b101;
				debugIndices[18] = 0b101;
				debugIndices[19] = 0b110;
				debugIndices[20] = 0b110;
				debugIndices[21] = 0b111;
				debugIndices[22] = 0b111;
				debugIndices[23] = 0b100;
				debugIndices[24] = 8u;
				debugIndices[25] = 9u;
			}
			auto debugIndexBuffer = utils->createFilledDeviceLocalGPUBufferOnDedMem(uploadQueue,tmp.size(),fillData);

			core::smart_refctd_ptr<video::IGPUDescriptorSetLayout> sharedDsLayout,debugDrawDsLayout;
			{
				video::IGPUDescriptorSetLayout::SBinding bnd[2];
				bnd[0].binding = 0u;
				bnd[0].count = 1u;
				bnd[0].type = asset::EDT_STORAGE_BUFFER;
				bnd[0].stageFlags = video::IGPUShader::ESS_COMPUTE;
				bnd[0].samplers = nullptr;
				bnd[1] = bnd[0];
				bnd[1].binding = 1u;
				sharedDsLayout = device->createGPUDescriptorSetLayout(bnd,bnd+2);
				debugDrawDsLayout = device->createGPUDescriptorSetLayout(bnd,bnd+1);
			}
			asset::IShader::E_SHADER_STAGE stageAccessFlags[ITransformTree::property_pool_t::PropertyCount];
			std::fill_n(stageAccessFlags,ITransformTree::property_pool_t::PropertyCount,asset::IShader::ESS_COMPUTE);
			auto poolLayout = ITransformTree::createDescriptorSetLayout(device,stageAccessFlags);
			
			auto updateRelativeLayout = device->createGPUPipelineLayout(nullptr,nullptr,core::smart_refctd_ptr(poolLayout),core::smart_refctd_ptr(sharedDsLayout));
			auto recomputeGlobalLayout = device->createGPUPipelineLayout(nullptr,nullptr,core::smart_refctd_ptr(poolLayout),core::smart_refctd_ptr(sharedDsLayout));
			asset::SPushConstantRange pcRange;
			pcRange.offset = 0u;
			pcRange.size = sizeof(DebugPushConstants);
			pcRange.stageFlags = asset::IShader::ESS_VERTEX;
			auto debugDrawLayout = device->createGPUPipelineLayout(nullptr,nullptr,core::smart_refctd_ptr(poolLayout),core::smart_refctd_ptr(debugDrawDsLayout));

			auto updateRelativePpln = device->createGPUComputePipeline(nullptr,std::move(updateRelativeLayout),std::move(updateRelativeSpec));
			auto recomputeGlobalPpln = device->createGPUComputePipeline(nullptr,std::move(recomputeGlobalLayout),std::move(recomputeGlobalSpec));
			core::smart_refctd_ptr<video::IGPURenderpassIndependentPipeline> debugDrawIndepenedentPipeline;
			{
				asset::SVertexInputParams vertexInputParams = {};
				vertexInputParams.bindings[DebugNodeIDBindingIndex].inputRate = asset::EVIR_PER_INSTANCE;
				vertexInputParams.bindings[DebugNodeIDBindingIndex].stride = sizeof(uint32_t);
				vertexInputParams.bindings[DebugAABBIDBindingIndex].inputRate = asset::EVIR_PER_INSTANCE;
				vertexInputParams.bindings[DebugAABBIDBindingIndex].stride = sizeof(uint32_t);

				vertexInputParams.attributes[DebugNodeIDAttributeIndex].binding = DebugNodeIDBindingIndex;
				vertexInputParams.attributes[DebugNodeIDAttributeIndex].format = asset::EF_R32_UINT;
				vertexInputParams.attributes[DebugAABBIDAttributeIndex].binding = DebugAABBIDBindingIndex;
				vertexInputParams.attributes[DebugAABBIDAttributeIndex].format = asset::EF_R32_UINT;

				vertexInputParams.enabledBindingFlags |= 0x1u<<DebugNodeIDBindingIndex;
				vertexInputParams.enabledBindingFlags |= 0x1u<<DebugAABBIDBindingIndex;
				vertexInputParams.enabledAttribFlags |= 0x1u<<DebugNodeIDAttributeIndex;
				vertexInputParams.enabledAttribFlags |= 0x1u<<DebugAABBIDAttributeIndex;

				asset::SBlendParams blendParams = {};
				asset::SPrimitiveAssemblyParams primitiveAssemblyParams = {};
				primitiveAssemblyParams.primitiveType = asset::EPT_LINE_LIST;
				asset::SRasterizationParams rasterizationParams = {};

				video::IGPUSpecializedShader* const debugDrawShaders[] = {debugDrawVertexSpec.get(),debugDrawFragmentSpec.get()};
				debugDrawIndepenedentPipeline = device->createGPURenderpassIndependentPipeline(
					nullptr,std::move(debugDrawLayout),debugDrawShaders,debugDrawShaders+2u,vertexInputParams,blendParams,primitiveAssemblyParams,rasterizationParams
				);
			}
			if (!updateRelativePpln || !recomputeGlobalPpln || !debugDrawIndepenedentPipeline)
				return nullptr;

			// TODO: after BaW
			core::smart_refctd_ptr<video::IGPUComputePipeline> updateAndRecomputePpln;
			
			// TODO: if we decide to invalidate all cmdbuffs used for updates (make them non reusable), then we can use the ECF_NONE flag
			auto descPool = device->createDescriptorPoolForDSLayouts(
				video::IDescriptorPool::ECF_UPDATE_AFTER_BIND_BIT,
				&sharedDsLayout.get(),&sharedDsLayout.get()+1u,
				&DescriptorCacheSize
			);
			auto descCache = core::make_smart_refctd_ptr<DescriptorSetCache>(device,std::move(descPool),std::move(sharedDsLayout));

			auto* ttm = new ITransformTreeManager(
				core::smart_refctd_ptr<video::ILogicalDevice>(device),std::move(descCache),
				std::move(updateRelativePpln),std::move(recomputeGlobalPpln),std::move(updateAndRecomputePpln),std::move(debugDrawIndepenedentPipeline),
				std::move(defaultFillValues),std::move(debugIndexBuffer)
			);
            return core::smart_refctd_ptr<ITransformTreeManager>(ttm,core::dont_grab);
        }

		static inline constexpr uint32_t TransferCount = 4u;
		struct RequestBase
		{
			ITransformTree* tree;
		};
		//
		struct TransferRequest : RequestBase
		{
			asset::SBufferRange<video::IGPUBuffer> nodes = {};
			// if not present we set these properties to defaults (no parent and identity transform)
			asset::SBufferBinding<video::IGPUBuffer> parents = {};
			asset::SBufferBinding<video::IGPUBuffer> relativeTransforms = {};
		};
		inline bool setupTransfers(const TransferRequest& request, video::CPropertyPoolHandler::TransferRequest* transfers)
		{
			if (!request.tree)
				return false;

			auto* pool = request.tree->getNodePropertyPool();

			for (auto i=0u; i<TransferCount; i++)
			{
				transfers[i].elementCount = request.nodes.size/sizeof(ITransformTree::node_t);
				transfers[i].srcAddressesOffset = video::IPropertyPool::invalid;
				transfers[i].dstAddressesOffset = request.nodes.offset;
			}
			transfers[0].setFromPool(pool,ITransformTree::parent_prop_ix);
			transfers[0].flags = request.parents.buffer ? video::CPropertyPoolHandler::TransferRequest::EF_NONE:video::CPropertyPoolHandler::TransferRequest::EF_FILL;
			transfers[0].buffer = request.parents.buffer ? request.parents:getDefaultValueBufferBinding(ITransformTree::parent_prop_ix);
			transfers[1].setFromPool(pool,ITransformTree::relative_transform_prop_ix);
			transfers[1].flags = request.relativeTransforms.buffer ? video::CPropertyPoolHandler::TransferRequest::EF_NONE:video::CPropertyPoolHandler::TransferRequest::EF_FILL;
			transfers[1].buffer = request.relativeTransforms.buffer ? request.relativeTransforms:getDefaultValueBufferBinding(ITransformTree::relative_transform_prop_ix);
			transfers[2].setFromPool(pool,ITransformTree::modified_stamp_prop_ix);
			transfers[2].flags = video::CPropertyPoolHandler::TransferRequest::EF_FILL;
			transfers[2].buffer = getDefaultValueBufferBinding(ITransformTree::modified_stamp_prop_ix);
			transfers[3].setFromPool(pool,ITransformTree::recomputed_stamp_prop_ix);
			transfers[3].flags = video::CPropertyPoolHandler::TransferRequest::EF_FILL;
			transfers[3].buffer = getDefaultValueBufferBinding(ITransformTree::recomputed_stamp_prop_ix);
			return true;
		}
		
		//
		struct UpstreamRequestBase : RequestBase
		{
			video::CPropertyPoolHandler::UpStreamingRequest::Source parents = {};
			video::CPropertyPoolHandler::UpStreamingRequest::Source relativeTransforms = {};
		};
		struct UpstreamRequest : UpstreamRequestBase
		{
			core::SRange<const ITransformTree::node_t> nodes = {nullptr,nullptr};
		};
		inline bool setupTransfers(const UpstreamRequest& request, video::CPropertyPoolHandler::UpStreamingRequest* upstreams)
		{
			if (!request.tree)
				return false;
			if (request.nodes.empty())
				return true;

			auto* pool = request.tree->getNodePropertyPool();

			for (auto i=0u; i<TransferCount; i++)
			{
				upstreams[i].elementCount = request.nodes.size();
				upstreams[i].srcAddresses = nullptr;
				upstreams[i].dstAddresses = request.nodes.begin();
			}
			upstreams[0].setFromPool(pool,ITransformTree::parent_prop_ix);
			if (request.parents.device2device || request.parents.data)
			{
				upstreams[0].fill = false;
				upstreams[0].source = request.parents;
			}
			else
			{
				upstreams[0].fill = true;
				upstreams[0].source.buffer = getDefaultValueBufferBinding(ITransformTree::parent_prop_ix);
			}
			upstreams[1].setFromPool(pool,ITransformTree::relative_transform_prop_ix);
			if (request.relativeTransforms.device2device || request.relativeTransforms.data)
			{
				upstreams[1].fill = false;
				upstreams[1].source = request.relativeTransforms;
			}
			else
			{
				upstreams[1].fill = true;
				upstreams[1].source.buffer = getDefaultValueBufferBinding(ITransformTree::relative_transform_prop_ix);
			}
			upstreams[2].setFromPool(pool,ITransformTree::modified_stamp_prop_ix);
			upstreams[2].fill = true;
			upstreams[2].source.buffer = getDefaultValueBufferBinding(ITransformTree::modified_stamp_prop_ix);
			upstreams[3].setFromPool(pool,ITransformTree::recomputed_stamp_prop_ix);
			upstreams[3].fill = true;
			upstreams[3].source.buffer = getDefaultValueBufferBinding(ITransformTree::recomputed_stamp_prop_ix);
			return true;
		}

		struct AdditionRequestBase
		{
			public:
				// must be in recording state
				video::IGPUCommandBuffer* cmdbuf;
				video::IGPUFence* fence;
				asset::SBufferBinding<video::IGPUBuffer> scratch;
				video::StreamingTransientDataBufferMT<>* upBuff;
				video::CPropertyPoolHandler* poolHandler;
				video::IGPUQueue* queue;
				system::logger_opt_ptr logger = nullptr;

			protected:
				inline bool isValid() const
				{
					return cmdbuf && fence && scratch.isValid() && upBuff && poolHandler && queue;
				}
		};
		//
		struct AdditionRequest : UpstreamRequestBase,AdditionRequestBase
		{
			// if the `outNodes` have values not equal to `invalid_node` then we treat them as already allocated
			// (this allows you to split allocation of nodes from setting up the transfers)
			core::SRange<ITransformTree::node_t> outNodes = {nullptr,nullptr};

			inline bool isValid() const
			{
				return AdditionRequestBase::isValid() && outNodes.begin() && outNodes.begin()<=outNodes.end();
			}
		};
		inline uint32_t addNodes(
			const AdditionRequest& request, uint32_t& waitSemaphoreCount,
			video::IGPUSemaphore* const*& semaphoresToWaitBeforeOverwrite,
			const asset::E_PIPELINE_STAGE_FLAGS*& stagesToWaitForPerSemaphore, 
			const std::chrono::steady_clock::time_point& maxWaitPoint=video::GPUEventWrapper::default_wait()
		)
		{
			if (!request.isValid())
				return false;
			if (request.outNodes.empty())
				return true;

			if (!request.tree->allocateNodes(request.outNodes))
				return false;

			video::CPropertyPoolHandler::UpStreamingRequest upstreams[TransferCount];
			UpstreamRequest req;
			static_cast<UpstreamRequestBase&>(req) = request;
			req.nodes = {request.outNodes.begin(),request.outNodes.end()};
			if (!setupTransfers(req,upstreams))
				return false;

			auto upstreamsPtr = upstreams;
			return request.poolHandler->transferProperties(
				request.upBuff,request.cmdbuf,request.fence,request.queue,request.scratch,upstreamsPtr,TransferCount,
				waitSemaphoreCount,semaphoresToWaitBeforeOverwrite,stagesToWaitForPerSemaphore,request.logger,maxWaitPoint
			);
		}

		//
		struct SkeletonAllocationRequest : RequestBase,AdditionRequestBase
		{
			core::SRange<const asset::ICPUSkeleton*> skeletons;
			// if nullptr then treated like a buffer of {1,1,...,1,1}, else needs to be same length as the skeleton range
			const uint32_t* instanceCounts = nullptr;
			// If you make the skeleton hierarchy have a real parent, you won't be able to share it amongst multiple instances of a mesh
			// also in order to render with standard shaders you'll have to cancel out the model transform of the parent for the skinning matrices.
			const ITransformTree::node_t*const * skeletonInstanceParents = nullptr;
			// the following arrays need to be sized according to `StagingRequirements`
			// if the `outNodes` have values not equal to `invalid_node` then we treat them as already allocated
			// (this allows you to split allocation of nodes from setting up the transfers)
			ITransformTree::node_t* outNodes = nullptr;
			// scratch buffers are just required to be the set size, they can be filled with garbage
			ITransformTree::node_t* parentScratch;
			// must be non null if at least one skeleton has default transforms
			ITransformTree::relative_transform_t* transformScratch = nullptr;

			inline bool isValid() const
			{
				return AdditionRequestBase::isValid() && skeletons.begin() && skeletons.begin()<=skeletons.end() && outNodes && parentScratch;
			}

			struct StagingRequirements
			{
				uint32_t nodeCount;
				uint32_t parentScratchSize;
				uint32_t transformScratchSize;
			};
			inline StagingRequirements computeStagingRequirements() const
			{
				StagingRequirements reqs = {0u,0u,0u};
				auto instanceCountIt = instanceCounts;
				for (auto skeleton : skeletons)
				{
					if (skeleton)
					{
						const uint32_t jointCount = skeleton->getJointCount();
						const uint32_t jointInstanceCount = (*instanceCountIt)*jointCount;
						reqs.nodeCount += jointInstanceCount;
						reqs.parentScratchSize += sizeof(ITransformTree::node_t)*jointInstanceCount;
						if (skeleton->getDefaultTransformBinding().buffer)
							reqs.transformScratchSize += sizeof(ITransformTree::relative_transform_t)*jointCount;
					}
					instanceCountIt++;
				}
				if (reqs.transformScratchSize)
					reqs.transformScratchSize += reqs.parentScratchSize*sizeof(uint32_t)/sizeof(ITransformTree::node_t);
				return reqs;
			}
		};
		inline bool addSkeletonNodes(
			const SkeletonAllocationRequest& request, uint32_t& waitSemaphoreCount,
			video::IGPUSemaphore* const*& semaphoresToWaitBeforeOverwrite,
			const asset::E_PIPELINE_STAGE_FLAGS*& stagesToWaitForPerSemaphore, 
			const std::chrono::steady_clock::time_point& maxWaitPoint=video::GPUEventWrapper::default_wait()
		)
		{
			if (!request.isValid())
				return false;

			const auto staging = request.computeStagingRequirements();
			if (staging.nodeCount==0u)
				return true;

			if (!request.tree->allocateNodes({request.outNodes,request.outNodes+staging.nodeCount}))
				return false;

			uint32_t* const srcTransformIndices = reinterpret_cast<uint32_t*>(request.transformScratch+staging.transformScratchSize/sizeof(ITransformTree::relative_transform_t));
			{
				auto instanceCountIt = request.instanceCounts;
				auto skeletonInstanceParentsIt = request.skeletonInstanceParents;
				auto parentsIt = request.parentScratch;
				auto transformIt = request.transformScratch;
				auto srcTransformIndicesIt = srcTransformIndices;
				uint32_t baseJointInstance = 0u;
				uint32_t baseJoint = 0u;
				for (auto skeleton : request.skeletons)
				{
					const auto instanceCount = request.instanceCounts ? (*(instanceCountIt++)):1u;
					auto* const instanceParents = request.skeletonInstanceParents ? (*(skeletonInstanceParentsIt++)):nullptr;

					const auto jointCount = skeleton->getJointCount();
					auto instanceParentsIt = instanceParents;
					for (auto instanceID=0u; instanceID<instanceCount; instanceID++)
					{
						for (auto jointID=0u; jointID<jointCount; jointID++)
						{
							uint32_t parentID = skeleton->getParentJointID(jointID);
							if (parentID!=asset::ICPUSkeleton::invalid_joint_id)
								parentID = request.outNodes[parentID+baseJointInstance];
							else
								parentID = instanceParents ? (*instanceParentsIt):ITransformTree::invalid_node;
							*(parentsIt++) = parentID;

							if (staging.transformScratchSize)
								*(srcTransformIndicesIt++) = jointID+baseJoint;
						}
						baseJointInstance += jointCount;
					}
					if (skeleton->getDefaultTransformBinding().buffer)
					for (auto jointID=0u; jointID<jointCount; jointID++)
						*(transformIt++) = skeleton->getDefaultTransformMatrix(jointID);
					baseJoint += jointCount;
				}
			}

			video::CPropertyPoolHandler::UpStreamingRequest upstreams[TransferCount];
			UpstreamRequest req = {};
			req.tree = request.tree;
			req.nodes = {request.outNodes,request.outNodes+staging.nodeCount};
			req.parents.data = request.parentScratch;
			if (staging.transformScratchSize)
				req.relativeTransforms.data = request.transformScratch;
			if (!setupTransfers(req,upstreams))
				return false;
			if (staging.transformScratchSize)
				upstreams[1].srcAddresses = srcTransformIndices;

			auto upstreamsPtr = upstreams;
			return request.poolHandler->transferProperties(
				request.upBuff,request.cmdbuf,request.fence,request.queue,request.scratch,upstreamsPtr,TransferCount,
				waitSemaphoreCount,semaphoresToWaitBeforeOverwrite,stagesToWaitForPerSemaphore,request.logger,maxWaitPoint
			);
		}


		 
		//
		inline void removeNodes(ITransformTree* tree, const ITransformTree::node_t* begin, const ITransformTree::node_t* end)
		{
			// If we start wanting a contiguous range to be maintained, this will need to change
			tree->getNodePropertyPool()->freeProperties(begin,end);
		}


		//
		struct SBarrierSuggestion
		{
			static inline constexpr uint32_t MaxBufferCount = 6u;

			//
			enum E_FLAG
			{
				// basic
				EF_PRE_RELATIVE_TFORM_UPDATE = 0x1u,
				EF_POST_RELATIVE_TFORM_UPDATE = 0x2u,
				EF_PRE_GLOBAL_TFORM_RECOMPUTE = 0x4u,
				EF_POST_GLOBAL_TFORM_RECOMPUTE = 0x8u,
				// if you plan to run recompute right after update
				EF_INBETWEEN_RLEATIVE_UPDATE_AND_GLOBAL_RECOMPUTE = EF_POST_RELATIVE_TFORM_UPDATE|EF_PRE_GLOBAL_TFORM_RECOMPUTE,
				// if you're planning to run the fused recompute and update kernel
				EF_PRE_UPDATE_AND_RECOMPUTE = EF_PRE_RELATIVE_TFORM_UPDATE|EF_PRE_GLOBAL_TFORM_RECOMPUTE,
				EF_POST_UPDATE_AND_RECOMPUTE = EF_POST_RELATIVE_TFORM_UPDATE|EF_POST_GLOBAL_TFORM_RECOMPUTE,
			};

			core::bitflag<asset::E_PIPELINE_STAGE_FLAGS> srcStageMask = static_cast<asset::E_PIPELINE_STAGE_FLAGS>(0u);
			core::bitflag<asset::E_PIPELINE_STAGE_FLAGS> dstStageMask = static_cast<asset::E_PIPELINE_STAGE_FLAGS>(0u);
			asset::SMemoryBarrier requestRanges = {};
			asset::SMemoryBarrier modificationRequests = {};
			asset::SMemoryBarrier relativeTransforms = {};
			asset::SMemoryBarrier modifiedTimestamps = {};
			asset::SMemoryBarrier globalTransforms = {};
			asset::SMemoryBarrier recomputedTimestamps = {};
		};
		//
		static inline SBarrierSuggestion barrierHelper(const SBarrierSuggestion::E_FLAG type)
		{
			const auto rwAccessMask = core::bitflag(asset::EAF_SHADER_READ_BIT)|asset::EAF_SHADER_WRITE_BIT;

			SBarrierSuggestion barrier;
			if (type&SBarrierSuggestion::EF_PRE_RELATIVE_TFORM_UPDATE)
			{
				// we're mostly concerned about stuff writing to buffer update reads from 
				barrier.dstStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				barrier.requestRanges.dstAccessMask |= asset::EAF_SHADER_READ_BIT;
				barrier.modificationRequests.dstAccessMask |= asset::EAF_SHADER_READ_BIT;
				// the case of update stepping on its own toes is handled by the POST case
			}
			if (type&SBarrierSuggestion::EF_POST_RELATIVE_TFORM_UPDATE)
			{
				// we're mostly concerned about relative tform update overwriting itself
				barrier.srcStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				barrier.dstStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				// we also need to barrier against any future update to the inputs overstepping our reading
				barrier.requestRanges.srcAccessMask |= asset::EAF_SHADER_READ_BIT;
				barrier.modificationRequests.srcAccessMask |= asset::EAF_SHADER_READ_BIT;
				// relative transform can be pre-post multiplied or entirely erased, we're not in charge of that
				// need to also worry about update<->update loop, so both masks are R/W
				barrier.relativeTransforms.srcAccessMask |= rwAccessMask;
				barrier.relativeTransforms.dstAccessMask |= rwAccessMask;
				// we will only overwrite
				barrier.modifiedTimestamps.srcAccessMask |= asset::EAF_SHADER_WRITE_BIT;
				// modified timestamp will be written by previous update, but also has to be read by recompute later
				barrier.modifiedTimestamps.dstAccessMask |= rwAccessMask;
				// we don't touch anything else
			}
			if (type&SBarrierSuggestion::EF_PRE_GLOBAL_TFORM_RECOMPUTE)
			{
				// we're mostly concerned about relative transform update not being finished before global transform recompute runs 
				barrier.srcStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				barrier.dstStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				barrier.relativeTransforms.srcAccessMask |= rwAccessMask;
				barrier.relativeTransforms.dstAccessMask |= asset::EAF_SHADER_READ_BIT;
				barrier.modifiedTimestamps.srcAccessMask |= asset::EAF_SHADER_WRITE_BIT;
				barrier.modifiedTimestamps.dstAccessMask |= asset::EAF_SHADER_READ_BIT;
			}
			if (type&SBarrierSuggestion::EF_POST_GLOBAL_TFORM_RECOMPUTE)
			{
				// we're mostly concerned about global tform recompute overwriting itself
				barrier.srcStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				barrier.dstStageMask |= asset::EPSF_COMPUTE_SHADER_BIT;
				// and future local update overwritng the inputs before recompute is done reading
				barrier.relativeTransforms.srcAccessMask |= asset::EAF_SHADER_READ_BIT;
				barrier.relativeTransforms.dstAccessMask |= rwAccessMask;
				barrier.modifiedTimestamps.srcAccessMask |= asset::EAF_SHADER_READ_BIT;
				barrier.modifiedTimestamps.dstAccessMask |= asset::EAF_SHADER_WRITE_BIT;
				// global transforms and recompute timestamps can be both read and written
				barrier.globalTransforms.srcAccessMask |= rwAccessMask;
				barrier.globalTransforms.dstAccessMask |= rwAccessMask;
				barrier.recomputedTimestamps.srcAccessMask |= rwAccessMask;
				barrier.recomputedTimestamps.dstAccessMask |= rwAccessMask;
			}
			return barrier;
		}

		//
		using ModificationRequestRange = nbl_glsl_transform_tree_modification_request_range_t;
		struct ParamsBase
		{
			ParamsBase()
			{
				dispatchIndirect.buffer = nullptr;
				dispatchDirect.nodeCount = 0u;
			}

			inline ParamsBase& operator=(const ParamsBase& other)
			{
				cmdbuf = other.cmdbuf;
				fence = other.fence;
				tree = other.tree;
				if (other.dispatchIndirect.buffer)
					dispatchIndirect = other.dispatchIndirect;
				else
				{
					dispatchIndirect.buffer = nullptr;
					dispatchDirect.nodeCount = other.dispatchDirect.nodeCount;
				}
				logger = other.logger;
				return *this;
			}

			video::IGPUCommandBuffer* cmdbuf; // must already be in recording state
			// for signalling when to drop a temporary descriptor set
			video::IGPUFence* fence;
			ITransformTree* tree;
			union
			{
				struct
				{
					video::IGPUBuffer* buffer;
					uint64_t offset;
				} dispatchIndirect;
				struct
				{
					private:
						uint64_t dummy;
					public:
						uint32_t nodeCount;
				} dispatchDirect;
			};
			system::logger_opt_ptr logger = nullptr;
		};

		//
		struct LocalTransformUpdateParams : ParamsBase
		{
			// first uint in the buffer tells us how many ModificationRequestRanges we have
			// second uint in the buffer tells us how many total requests we have
			// rest is filled wtih ModificationRequestRange
			asset::SBufferBinding<video::IGPUBuffer> requestRanges;
			// this one is filled with RelativeTransformModificationRequest
			asset::SBufferBinding<video::IGPUBuffer> modificationRequests;
		};
		inline bool updateLocalTransforms(const LocalTransformUpdateParams& params)
		{
			return soleUpdateOrFusedRecompute_impl<2u>(m_updatePipeline.get(),params,{params.requestRanges,params.modificationRequests});
		}

		//
		struct GlobalTransformUpdateParams : ParamsBase
		{
			// first uint in the buffer tells us how many nodes to update we have
			asset::SBufferBinding<video::IGPUBuffer> nodeIDs; // imo it should be SBufferRange
		};
		inline bool recomputeGlobalTransforms(const GlobalTransformUpdateParams& params)
		{
			return soleUpdateOrFusedRecompute_impl<1u>(m_recomputePipeline.get(),params,{params.nodeIDs});
		}


		static inline constexpr uint32_t DebugNodeIDAttributeIndex = 14u;
		static inline constexpr uint32_t DebugAABBIDAttributeIndex = 15u;
		static inline constexpr uint32_t DebugNodeIDBindingIndex = DebugNodeIDAttributeIndex;
		static inline constexpr uint32_t DebugAABBIDBindingIndex = DebugAABBIDAttributeIndex;
		//
		inline auto createDebugPipeline(core::smart_refctd_ptr<video::IGPURenderpass>&& renderpass)
		{
			nbl::video::IGPUGraphicsPipeline::SCreationParams graphicsPipelineParams;
			graphicsPipelineParams.renderpassIndependent = m_debugDrawIndepenedentPipeline;
			graphicsPipelineParams.renderpass = std::move(renderpass);
			return m_device->createGPUGraphicsPipeline(nullptr,std::move(graphicsPipelineParams));
		}

		//
		struct DebugPushConstants
		{
			core::matrix4SIMD viewProjectionMatrix;
			core::vector4df_SIMD lineColor;
			core::vector4df_SIMD aabbColor;
		};
		inline void debugDraw(
			video::IGPUCommandBuffer* cmdbuf, const video::IGPUGraphicsPipeline* pipeline, const ITransformTree* tree, const video::IGPUDescriptorSet* aabb,
			const asset::SBufferBinding<video::IGPUBuffer>& nodeID, const asset::SBufferBinding<video::IGPUBuffer>& aabbID,
			const DebugPushConstants& pushConstants, const uint32_t count
		)
		{
			auto layout = m_debugDrawIndepenedentPipeline->getLayout();
			assert(pipeline->getRenderpassIndependentPipeline()->getLayout()==layout);

			const video::IGPUDescriptorSet* sets[] = {tree->getNodePropertyDescriptorSet(),aabb};
			cmdbuf->bindDescriptorSets(asset::EPBP_GRAPHICS,layout,0u,2u,sets);
			cmdbuf->bindGraphicsPipeline(pipeline);
			{
				auto buffer = nodeID.buffer.get();
				size_t offset = nodeID.offset;
				cmdbuf->bindVertexBuffers(DebugNodeIDBindingIndex,1u,&buffer,&offset);
				buffer = aabbID.buffer.get();
				offset = aabbID.offset;
				cmdbuf->bindVertexBuffers(DebugAABBIDBindingIndex,1u,&buffer,&offset);
			}
			cmdbuf->bindIndexBuffer(m_debugIndexBuffer.get(),0u,asset::EIT_16BIT);
			cmdbuf->pushConstants(layout,asset::IShader::ESS_VERTEX,0u,sizeof(DebugPushConstants),&pushConstants);
			cmdbuf->drawIndexed(IndexCount,count,0u,0u,0u);
		}

	protected:
		static inline uint64_t getDefaultValueBufferOffset(const video::IPhysicalDevice::SLimits& limits, uint32_t prop_ix)
		{
			uint64_t offset = 0u;
			const uint64_t ssboOffsetAlignment = limits.SSBOAlignment;
			if (prop_ix!=ITransformTree::relative_transform_prop_ix)
			{
				offset = core::roundUp(offset+sizeof(ITransformTree::relative_transform_t),ssboOffsetAlignment);
				if (prop_ix!=ITransformTree::parent_prop_ix)
				{
					offset = core::roundUp(offset+sizeof(ITransformTree::parent_t),ssboOffsetAlignment);
					if (prop_ix!=ITransformTree::modified_stamp_prop_ix)
					{
						offset = core::roundUp(offset+sizeof(ITransformTree::modified_stamp_t),ssboOffsetAlignment);
						if (prop_ix!=ITransformTree::recomputed_stamp_prop_ix)
							return core::roundUp(offset+sizeof(ITransformTree::recomputed_stamp_t),ssboOffsetAlignment);
					}
				}
			}
			return offset;
		}

		static inline constexpr auto DescriptorCacheSize = 32u;
		// TODO: investigate using Push Descriptors for this
		class DescriptorSetCache : public video::IDescriptorSetCache
		{
			public:
				using IDescriptorSetCache::IDescriptorSetCache;

				static constexpr inline auto SharedBindingCount = 3u;

				uint32_t acquireSet(video::ILogicalDevice* device, const asset::SBufferRange<video::IGPUBuffer>* buffers, uint32_t count)
				{
					auto retval = IDescriptorSetCache::acquireSet();
					if (retval==IDescriptorSetCache::invalid_index)
						return IDescriptorSetCache::invalid_index;
					video::IGPUDescriptorSet* set = IDescriptorSetCache::getSet(retval);

					video::IGPUDescriptorSet::SWriteDescriptorSet writes[SharedBindingCount];
					video::IGPUDescriptorSet::SDescriptorInfo infos[SharedBindingCount];
					for (auto i=0u; i<count; i++)
					{
						infos[i].desc = buffers[i].buffer;
						infos[i].buffer.offset = buffers[i].offset;
						infos[i].buffer.size = buffers[i].size;
						writes[i].dstSet = set;
						writes[i].binding = i;
						writes[i].arrayElement = 0u;
						writes[i].count = 1u;
						writes[i].descriptorType = asset::EDT_STORAGE_BUFFER;
						writes[i].info = infos+i;
					}
					device->updateDescriptorSets(count,writes,0u,nullptr);

					return retval;
				}
		};
		ITransformTreeManager(
			core::smart_refctd_ptr<video::ILogicalDevice>&& _device,
			core::smart_refctd_ptr<DescriptorSetCache>&& _dsCache,
			core::smart_refctd_ptr<video::IGPUComputePipeline>&& _updatePipeline,
			core::smart_refctd_ptr<video::IGPUComputePipeline>&& _recomputePipeline,
			core::smart_refctd_ptr<video::IGPUComputePipeline>&& _updateAndRecomputePipeline,
			core::smart_refctd_ptr<video::IGPURenderpassIndependentPipeline>&& _debugDrawIndepenedentPipeline,
			core::smart_refctd_ptr<video::IGPUBuffer>&& _defaultFillValues,
			core::smart_refctd_ptr<video::IGPUBuffer>&& _debugIndexBuffer
		) : m_device(std::move(_device)), m_dsCache(std::move(_dsCache)),
			m_updatePipeline(std::move(_updatePipeline)),
			m_recomputePipeline(std::move(_recomputePipeline)),
			m_updateAndRecomputePipeline(std::move(_updateAndRecomputePipeline)),
			m_debugDrawIndepenedentPipeline(_debugDrawIndepenedentPipeline),
			m_defaultFillValues(std::move(_defaultFillValues)),
			m_debugIndexBuffer(std::move(_debugIndexBuffer)),
			m_workgroupSize(m_device->getPhysicalDevice()->getLimits().maxOptimallyResidentWorkgroupInvocations)
		{
		}
		~ITransformTreeManager()
		{
			// everything drops itself automatically
		}
		
		inline asset::SBufferBinding<video::IGPUBuffer> getDefaultValueBufferBinding(uint32_t prop_ix) const
		{
			const auto& limits = m_device->getPhysicalDevice()->getLimits();
			return {getDefaultValueBufferOffset(limits,prop_ix),m_defaultFillValues};
		}
		
		template<uint32_t N>
		bool soleUpdateOrFusedRecompute_impl(const video::IGPUComputePipeline* pipeline, const ParamsBase& params, const std::array<asset::SBufferBinding<video::IGPUBuffer>,N>& bufferBindings)
		{
			auto* cmdbuf = params.cmdbuf;

			auto dsix = m_dsCache->acquireSet();
			if (dsix==video::IDescriptorSetCache::invalid_index)
			{
				params.logger.log("CPropertyPoolHandler: Failed to acquire descriptor set!",system::ILogger::ELL_ERROR);
				return false;
			}
			video::IGPUDescriptorSet* tempDS = m_dsCache->getSet(dsix);
			{
				constexpr auto MaxBindingCount = 2u;

				video::IGPUDescriptorSet::SDescriptorInfo info[MaxBindingCount];
				for (auto i=0u; i<N; i++)
				{
					info[i].desc = bufferBindings[i].buffer;
					info[i].buffer.offset = bufferBindings[i].offset;
					info[i].buffer.size = video::IGPUDescriptorSet::SDescriptorInfo::SBufferInfo::WholeBuffer;
				}
				video::IGPUDescriptorSet::SWriteDescriptorSet w[MaxBindingCount];
				for (auto i=0u; i<MaxBindingCount; i++)
				{
					w[i].arrayElement = 0u;
					w[i].binding = i;
					w[i].count = 1u;
					w[i].descriptorType = asset::EDT_STORAGE_BUFFER;
					w[i].info = info+std::min(i,N-1u);
					w[i].dstSet = tempDS;
				}
				m_device->updateDescriptorSets(MaxBindingCount,w,0u,nullptr);
			}
			const video::IGPUDescriptorSet* descSets[] = { params.tree->getNodePropertyDescriptorSet(),tempDS };
			cmdbuf->bindDescriptorSets(asset::EPBP_COMPUTE,pipeline->getLayout(),0u,2u,descSets,nullptr);
			
			cmdbuf->bindComputePipeline(pipeline);
			if (params.dispatchIndirect.buffer)
				cmdbuf->dispatchIndirect(params.dispatchIndirect.buffer,params.dispatchIndirect.offset);
			else
			{
				const auto& limits = m_device->getPhysicalDevice()->getLimits();
				cmdbuf->dispatch(limits.computeOptimalPersistentWorkgroupDispatchSize(params.dispatchDirect.nodeCount,m_workgroupSize),1u,1u);
			}

			m_dsCache->releaseSet(m_device.get(),core::smart_refctd_ptr<video::IGPUFence>(params.fence),dsix);
			return true;
		}

		core::smart_refctd_ptr<video::ILogicalDevice> m_device;
		core::smart_refctd_ptr<video::IDescriptorSetCache> m_dsCache;
		core::smart_refctd_ptr<video::IGPUComputePipeline> m_updatePipeline,m_recomputePipeline,m_updateAndRecomputePipeline;
		core::smart_refctd_ptr<video::IGPUBuffer> m_defaultFillValues;
		const uint32_t m_workgroupSize;

		core::smart_refctd_ptr<video::IGPURenderpassIndependentPipeline> m_debugDrawIndepenedentPipeline;
		core::smart_refctd_ptr<video::IGPUBuffer> m_debugIndexBuffer;
		constexpr static inline auto AABBIndices = 24u;
		constexpr static inline auto LineIndices = 2u;
		constexpr static inline auto IndexCount = AABBIndices+LineIndices;
};

} // end namespace nbl::scene

#endif

