// Every feature of Features' four structs, by name (context.cpp's required
// features: checked, enabled, named when missing). Checked complete by
// static_asserts there: a field left out is a build error.

#define BVK_CORE_FEATURES(X)                                                                                       \
  X(robustBufferAccess) X(fullDrawIndexUint32) X(imageCubeArray) X(independentBlend) X(geometryShader)             \
  X(tessellationShader) X(sampleRateShading) X(dualSrcBlend) X(logicOp) X(multiDrawIndirect)                       \
  X(drawIndirectFirstInstance) X(depthClamp) X(depthBiasClamp) X(fillModeNonSolid) X(depthBounds) X(wideLines)     \
  X(largePoints) X(alphaToOne) X(multiViewport) X(samplerAnisotropy) X(textureCompressionETC2)                     \
  X(textureCompressionASTC_LDR) X(textureCompressionBC) X(occlusionQueryPrecise) X(pipelineStatisticsQuery)        \
  X(vertexPipelineStoresAndAtomics) X(fragmentStoresAndAtomics) X(shaderTessellationAndGeometryPointSize)          \
  X(shaderImageGatherExtended) X(shaderStorageImageExtendedFormats) X(shaderStorageImageMultisample)               \
  X(shaderStorageImageReadWithoutFormat) X(shaderStorageImageWriteWithoutFormat)                                   \
  X(shaderUniformBufferArrayDynamicIndexing) X(shaderSampledImageArrayDynamicIndexing)                             \
  X(shaderStorageBufferArrayDynamicIndexing) X(shaderStorageImageArrayDynamicIndexing) X(shaderClipDistance)       \
  X(shaderCullDistance) X(shaderFloat64) X(shaderInt64) X(shaderInt16) X(shaderResourceResidency)                  \
  X(shaderResourceMinLod) X(sparseBinding) X(sparseResidencyBuffer) X(sparseResidencyImage2D)                      \
  X(sparseResidencyImage3D) X(sparseResidency2Samples) X(sparseResidency4Samples) X(sparseResidency8Samples)       \
  X(sparseResidency16Samples) X(sparseResidencyAliased) X(variableMultisampleRate) X(inheritedQueries)

#define BVK_V11_FEATURES(X)                                                                                        \
  X(storageBuffer16BitAccess) X(uniformAndStorageBuffer16BitAccess) X(storagePushConstant16)                       \
  X(storageInputOutput16) X(multiview) X(multiviewGeometryShader) X(multiviewTessellationShader)                   \
  X(variablePointersStorageBuffer) X(variablePointers) X(protectedMemory) X(samplerYcbcrConversion)                \
  X(shaderDrawParameters)

#define BVK_V12_FEATURES(X)                                                                                        \
  X(samplerMirrorClampToEdge) X(drawIndirectCount) X(storageBuffer8BitAccess) X(uniformAndStorageBuffer8BitAccess) \
  X(storagePushConstant8) X(shaderBufferInt64Atomics) X(shaderSharedInt64Atomics) X(shaderFloat16) X(shaderInt8)   \
  X(descriptorIndexing) X(shaderInputAttachmentArrayDynamicIndexing)                                               \
  X(shaderUniformTexelBufferArrayDynamicIndexing) X(shaderStorageTexelBufferArrayDynamicIndexing)                  \
  X(shaderUniformBufferArrayNonUniformIndexing) X(shaderSampledImageArrayNonUniformIndexing)                       \
  X(shaderStorageBufferArrayNonUniformIndexing) X(shaderStorageImageArrayNonUniformIndexing)                       \
  X(shaderInputAttachmentArrayNonUniformIndexing) X(shaderUniformTexelBufferArrayNonUniformIndexing)               \
  X(shaderStorageTexelBufferArrayNonUniformIndexing) X(descriptorBindingUniformBufferUpdateAfterBind)              \
  X(descriptorBindingSampledImageUpdateAfterBind) X(descriptorBindingStorageImageUpdateAfterBind)                  \
  X(descriptorBindingStorageBufferUpdateAfterBind) X(descriptorBindingUniformTexelBufferUpdateAfterBind)           \
  X(descriptorBindingStorageTexelBufferUpdateAfterBind) X(descriptorBindingUpdateUnusedWhilePending)               \
  X(descriptorBindingPartiallyBound) X(descriptorBindingVariableDescriptorCount) X(runtimeDescriptorArray)         \
  X(samplerFilterMinmax) X(scalarBlockLayout) X(imagelessFramebuffer) X(uniformBufferStandardLayout)               \
  X(shaderSubgroupExtendedTypes) X(separateDepthStencilLayouts) X(hostQueryReset) X(timelineSemaphore)             \
  X(bufferDeviceAddress) X(bufferDeviceAddressCaptureReplay) X(bufferDeviceAddressMultiDevice)                     \
  X(vulkanMemoryModel) X(vulkanMemoryModelDeviceScope) X(vulkanMemoryModelAvailabilityVisibilityChains)            \
  X(shaderOutputViewportIndex) X(shaderOutputLayer) X(subgroupBroadcastDynamicId)

#define BVK_V13_FEATURES(X)                                                                                        \
  X(robustImageAccess) X(inlineUniformBlock) X(descriptorBindingInlineUniformBlockUpdateAfterBind)                 \
  X(pipelineCreationCacheControl) X(privateData) X(shaderDemoteToHelperInvocation) X(shaderTerminateInvocation)    \
  X(subgroupSizeControl) X(computeFullSubgroups) X(synchronization2) X(textureCompressionASTC_HDR)                 \
  X(shaderZeroInitializeWorkgroupMemory) X(dynamicRendering) X(shaderIntegerDotProduct) X(maintenance4)
