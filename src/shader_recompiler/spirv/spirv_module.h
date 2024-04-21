#pragma once

#include <unordered_set>
#include "common/types.h"
#include "shader_recompiler/spirv/spirv_code_buffer.h"

namespace Shader::Gcn {

struct SpirvPhiLabel {
    u32 varId = 0;
    u32 labelId = 0;
};

struct SpirvSwitchCaseLabel {
    u32 literal = 0;
    u32 labelId = 0;
};

struct SpirvImageOperands {
    u32 flags = 0;
    u32 sLodBias = 0;
    u32 sLod = 0;
    u32 sConstOffset = 0;
    u32 sGradX = 0;
    u32 sGradY = 0;
    u32 gOffset = 0;
    u32 gConstOffsets = 0;
    u32 sSampleId = 0;
    u32 sMinLod = 0;
};

constexpr u32 spvVersion(u32 major, u32 minor) {
    return (major << 16) | (minor << 8);
}

/**
 * \brief SPIR-V module
 *
 * This class generates a code buffer containing a full
 * SPIR-V shader module. Ensures that the module layout
 * is valid, as defined in the SPIR-V 1.0 specification,
 * section 2.4 "Logical Layout of a Module".
 */
class SpirvModule {

public:
    explicit SpirvModule(u32 version);

    ~SpirvModule();

    SpirvCodeBuffer compile() const;

    size_t getInsertionPtr() {
        return m_code.getInsertionPtr();
    }

    void beginInsertion(size_t ptr) {
        m_code.beginInsertion(ptr);
    }

    void endInsertion() {
        m_code.endInsertion();
    }

    u32 allocateId();

    bool hasCapability(spv::Capability capability);

    void enableCapability(spv::Capability capability);

    void enableExtension(const char* extensionName);

    void addEntryPoint(u32 entryPointId, spv::ExecutionModel executionModel, const char* name,
                       u32 interfaceCount, const u32* interfaceIds);

    void setMemoryModel(spv::AddressingModel addressModel, spv::MemoryModel memoryModel);

    void setExecutionMode(u32 entryPointId, spv::ExecutionMode executionMode);

    void setExecutionMode(u32 entryPointId, spv::ExecutionMode executionMode, u32 argCount,
                          const u32* args);

    void setInvocations(u32 entryPointId, u32 invocations);

    void setLocalSize(u32 entryPointId, u32 x, u32 y, u32 z);

    void setOutputVertices(u32 entryPointId, u32 vertexCount);

    u32 addDebugString(const char* string);

    void setDebugSource(spv::SourceLanguage language, u32 version, u32 file, const char* source);

    void setDebugName(u32 expressionId, const char* debugName);

    void setDebugMemberName(u32 structId, u32 memberId, const char* debugName);

    u32 constBool(bool v);

    u32 consti32(int32_t v);

    u32 consti64(int64_t v);

    u32 constu32(u32 v);

    u32 constu64(uint64_t v);

    u32 constf32(float v);

    u32 constf64(double v);

    u32 constvec4i32(int32_t x, int32_t y, int32_t z, int32_t w);

    u32 constvec4b32(bool x, bool y, bool z, bool w);

    u32 constvec4u32(u32 x, u32 y, u32 z, u32 w);

    u32 constvec2f32(float x, float y);

    u32 constvec3f32(float x, float y, float z);

    u32 constvec4f32(float x, float y, float z, float w);

    u32 constfReplicant(float replicant, u32 count);

    u32 constbReplicant(bool replicant, u32 count);

    u32 constiReplicant(int32_t replicant, u32 count);

    u32 constuReplicant(int32_t replicant, u32 count);

    u32 constComposite(u32 typeId, u32 constCount, const u32* constIds);

    u32 constUndef(u32 typeId);

    u32 lateConst32(u32 typeId);

    void setLateConst(u32 constId, const u32* argIds);

    u32 specConstBool(bool v);

    u32 specConst32(u32 typeId, u32 value);

    void decorate(u32 object, spv::Decoration decoration);

    void decorateArrayStride(u32 object, u32 stride);

    void decorateBinding(u32 object, u32 binding);

    void decorateBlock(u32 object);

    void decorateBuiltIn(u32 object, spv::BuiltIn builtIn);

    void decorateComponent(u32 object, u32 location);

    void decorateDescriptorSet(u32 object, u32 set);

    void decorateIndex(u32 object, u32 index);

    void decorateLocation(u32 object, u32 location);

    void decorateSpecId(u32 object, u32 specId);

    void decorateXfb(u32 object, u32 streamId, u32 bufferId, u32 offset, u32 stride);

    void memberDecorateBuiltIn(u32 structId, u32 memberId, spv::BuiltIn builtIn);

    void memberDecorate(u32 structId, u32 memberId, spv::Decoration decoration);

    void memberDecorateMatrixStride(u32 structId, u32 memberId, u32 stride);

    void memberDecorateOffset(u32 structId, u32 memberId, u32 offset);

    u32 defVoidType();

    u32 defBoolType();

    u32 defIntType(u32 width, u32 isSigned);

    u32 defFloatType(u32 width);

    u32 defVectorType(u32 elementType, u32 elementCount);

    u32 defMatrixType(u32 columnType, u32 columnCount);

    u32 defArrayType(u32 typeId, u32 length);

    u32 defArrayTypeUnique(u32 typeId, u32 length);

    u32 defRuntimeArrayType(u32 typeId);

    u32 defRuntimeArrayTypeUnique(u32 typeId);

    u32 defFunctionType(u32 returnType, u32 argCount, const u32* argTypes);

    u32 defStructType(u32 memberCount, const u32* memberTypes);

    u32 defStructTypeUnique(u32 memberCount, const u32* memberTypes);

    u32 defPointerType(u32 variableType, spv::StorageClass storageClass);

    u32 defSamplerType();

    u32 defImageType(u32 sampledType, spv::Dim dimensionality, u32 depth, u32 arrayed,
                     u32 multisample, u32 sampled, spv::ImageFormat format);

    u32 defSampledImageType(u32 imageType);

    u32 newVar(u32 pointerType, spv::StorageClass storageClass);

    u32 newVarInit(u32 pointerType, spv::StorageClass storageClass, u32 initialValue);

    void functionBegin(u32 returnType, u32 functionId, u32 functionType,
                       spv::FunctionControlMask functionControl);

    u32 functionParameter(u32 parameterType);

    void functionEnd();

    u32 opAccessChain(u32 resultType, u32 composite, u32 indexCount, const u32* indexArray);

    u32 opArrayLength(u32 resultType, u32 structure, u32 memberId);

    u32 opAny(u32 resultType, u32 vector);

    u32 opAll(u32 resultType, u32 vector);

    u32 opAtomicLoad(u32 resultType, u32 pointer, u32 scope, u32 semantics);

    void opAtomicStore(u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicExchange(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicCompareExchange(u32 resultType, u32 pointer, u32 scope, u32 equal, u32 unequal,
                                u32 value, u32 comparator);

    u32 opAtomicIIncrement(u32 resultType, u32 pointer, u32 scope, u32 semantics);

    u32 opAtomicIDecrement(u32 resultType, u32 pointer, u32 scope, u32 semantics);

    u32 opAtomicIAdd(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicISub(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicSMin(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicSMax(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicUMin(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicUMax(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicAnd(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicOr(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opAtomicXor(u32 resultType, u32 pointer, u32 scope, u32 semantics, u32 value);

    u32 opBitcast(u32 resultType, u32 operand);

    u32 opBitCount(u32 resultType, u32 operand);

    u32 opBitReverse(u32 resultType, u32 operand);

    u32 opFindILsb(u32 resultType, u32 operand);

    u32 opFindUMsb(u32 resultType, u32 operand);

    u32 opFindSMsb(u32 resultType, u32 operand);

    u32 opBitFieldInsert(u32 resultType, u32 base, u32 insert, u32 offset, u32 count);

    u32 opBitFieldSExtract(u32 resultType, u32 base, u32 offset, u32 count);

    u32 opBitFieldUExtract(u32 resultType, u32 base, u32 offset, u32 count);

    u32 opBitwiseAnd(u32 resultType, u32 operand1, u32 operand2);

    u32 opBitwiseOr(u32 resultType, u32 operand1, u32 operand2);

    u32 opBitwiseXor(u32 resultType, u32 operand1, u32 operand2);

    u32 opNot(u32 resultType, u32 operand);

    u32 opShiftLeftLogical(u32 resultType, u32 base, u32 shift);

    u32 opShiftRightArithmetic(u32 resultType, u32 base, u32 shift);

    u32 opShiftRightLogical(u32 resultType, u32 base, u32 shift);

    u32 opConvertFtoS(u32 resultType, u32 operand);

    u32 opConvertFtoU(u32 resultType, u32 operand);

    u32 opConvertStoF(u32 resultType, u32 operand);

    u32 opConvertUtoF(u32 resultType, u32 operand);

    u32 opCompositeConstruct(u32 resultType, u32 valueCount, const u32* valueArray);

    u32 opCompositeExtract(u32 resultType, u32 composite, u32 indexCount, const u32* indexArray);

    u32 opCompositeInsert(u32 resultType, u32 object, u32 composite, u32 indexCount,
                          const u32* indexArray);

    u32 opDpdx(u32 resultType, u32 operand);

    u32 opDpdy(u32 resultType, u32 operand);

    u32 opDpdxCoarse(u32 resultType, u32 operand);

    u32 opDpdyCoarse(u32 resultType, u32 operand);

    u32 opDpdxFine(u32 resultType, u32 operand);

    u32 opDpdyFine(u32 resultType, u32 operand);

    u32 opVectorExtractDynamic(u32 resultType, u32 vector, u32 index);

    u32 opVectorShuffle(u32 resultType, u32 vectorLeft, u32 vectorRight, u32 indexCount,
                        const u32* indexArray);

    u32 opSNegate(u32 resultType, u32 operand);

    u32 opFNegate(u32 resultType, u32 operand);

    u32 opSAbs(u32 resultType, u32 operand);

    u32 opFAbs(u32 resultType, u32 operand);

    u32 opFSign(u32 resultType, u32 operand);

    u32 opFMix(u32 resultType, u32 x, u32 y, u32 a);

    u32 opCross(u32 resultType, u32 x, u32 y);

    u32 opIAdd(u32 resultType, u32 a, u32 b);

    u32 opIAddCarry(u32 resultType, u32 a, u32 b);

    u32 opISub(u32 resultType, u32 a, u32 b);

    u32 opFAdd(u32 resultType, u32 a, u32 b);

    u32 opFSub(u32 resultType, u32 a, u32 b);

    u32 opSDiv(u32 resultType, u32 a, u32 b);

    u32 opUDiv(u32 resultType, u32 a, u32 b);

    u32 opSRem(u32 resultType, u32 a, u32 b);

    u32 opUMod(u32 resultType, u32 a, u32 b);

    u32 opFDiv(u32 resultType, u32 a, u32 b);

    u32 opIMul(u32 resultType, u32 a, u32 b);

    u32 opUMulExtended(u32 resultType, u32 a, u32 b);

    u32 opSMulExtended(u32 resultType, u32 a, u32 b);

    u32 opFMul(u32 resultType, u32 a, u32 b);

    u32 opVectorTimesScalar(u32 resultType, u32 vector, u32 scalar);

    u32 opMatrixTimesMatrix(u32 resultType, u32 a, u32 b);

    u32 opMatrixTimesVector(u32 resultType, u32 matrix, u32 vector);

    u32 opVectorTimesMatrix(u32 resultType, u32 vector, u32 matrix);

    u32 opTranspose(u32 resultType, u32 matrix);

    u32 opMatrixInverse(u32 resultType, u32 matrix);

    u32 opFFma(u32 resultType, u32 a, u32 b, u32 c);

    u32 opFMax(u32 resultType, u32 a, u32 b);

    u32 opFMin(u32 resultType, u32 a, u32 b);

    u32 opNMax(u32 resultType, u32 a, u32 b);

    u32 opNMin(u32 resultType, u32 a, u32 b);

    u32 opSMax(u32 resultType, u32 a, u32 b);

    u32 opSMin(u32 resultType, u32 a, u32 b);

    u32 opUMax(u32 resultType, u32 a, u32 b);

    u32 opUMin(u32 resultType, u32 a, u32 b);

    u32 opFClamp(u32 resultType, u32 x, u32 minVal, u32 maxVal);

    u32 opNClamp(u32 resultType, u32 x, u32 minVal, u32 maxVal);

    u32 opIEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opINotEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opSLessThan(u32 resultType, u32 vector1, u32 vector2);

    u32 opSLessThanEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opSGreaterThan(u32 resultType, u32 vector1, u32 vector2);

    u32 opSGreaterThanEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opULessThan(u32 resultType, u32 vector1, u32 vector2);

    u32 opULessThanEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opUGreaterThan(u32 resultType, u32 vector1, u32 vector2);

    u32 opUGreaterThanEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opFOrdEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opFOrdNotEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opFOrdLessThan(u32 resultType, u32 vector1, u32 vector2);

    u32 opFOrdLessThanEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opFOrdGreaterThan(u32 resultType, u32 vector1, u32 vector2);

    u32 opFOrdGreaterThanEqual(u32 resultType, u32 vector1, u32 vector2);

    u32 opLogicalEqual(u32 resultType, u32 operand1, u32 operand2);

    u32 opLogicalNotEqual(u32 resultType, u32 operand1, u32 operand2);

    u32 opLogicalAnd(u32 resultType, u32 operand1, u32 operand2);

    u32 opLogicalOr(u32 resultType, u32 operand1, u32 operand2);

    u32 opLogicalNot(u32 resultType, u32 operand);

    u32 opDot(u32 resultType, u32 vector1, u32 vector2);

    u32 opSin(u32 resultType, u32 vector);

    u32 opCos(u32 resultType, u32 vector);

    u32 opSqrt(u32 resultType, u32 operand);

    u32 opInverseSqrt(u32 resultType, u32 operand);

    u32 opNormalize(u32 resultType, u32 operand);

    u32 opReflect(u32 resultType, u32 incident, u32 normal);

    u32 opLength(u32 resultType, u32 operand);

    u32 opExp2(u32 resultType, u32 operand);

    u32 opExp(u32 resultType, u32 operand);

    u32 opLog2(u32 resultType, u32 operand);

    u32 opPow(u32 resultType, u32 base, u32 exponent);

    u32 opFract(u32 resultType, u32 operand);

    u32 opCeil(u32 resultType, u32 operand);

    u32 opFloor(u32 resultType, u32 operand);

    u32 opRound(u32 resultType, u32 operand);

    u32 opRoundEven(u32 resultType, u32 operand);

    u32 opTrunc(u32 resultType, u32 operand);

    u32 opFConvert(u32 resultType, u32 operand);

    u32 opPackHalf2x16(u32 resultType, u32 operand);

    u32 opUnpackHalf2x16(u32 resultType, u32 operand);

    u32 opSelect(u32 resultType, u32 condition, u32 operand1, u32 operand2);

    u32 opIsNan(u32 resultType, u32 operand);

    u32 opIsInf(u32 resultType, u32 operand);

    u32 opFunctionCall(u32 resultType, u32 functionId, u32 argCount, const u32* argIds);

    void opLabel(u32 labelId);

    u32 opLoad(u32 typeId, u32 pointerId);

    void opStore(u32 pointerId, u32 valueId);

    u32 opInterpolateAtCentroid(u32 resultType, u32 interpolant);

    u32 opInterpolateAtSample(u32 resultType, u32 interpolant, u32 sample);

    u32 opInterpolateAtOffset(u32 resultType, u32 interpolant, u32 offset);

    u32 opImage(u32 resultType, u32 sampledImage);

    u32 opImageRead(u32 resultType, u32 image, u32 coordinates, const SpirvImageOperands& operands);

    void opImageWrite(u32 image, u32 coordinates, u32 texel, const SpirvImageOperands& operands);

    u32 opImageTexelPointer(u32 resultType, u32 image, u32 coordinates, u32 sample);

    u32 opSampledImage(u32 resultType, u32 image, u32 sampler);

    u32 opImageQuerySizeLod(u32 resultType, u32 image, u32 lod);

    u32 opImageQuerySize(u32 resultType, u32 image);

    u32 opImageQueryLevels(u32 resultType, u32 image);

    u32 opImageQueryLod(u32 resultType, u32 sampledImage, u32 coordinates);

    u32 opImageQuerySamples(u32 resultType, u32 image);

    u32 opImageFetch(u32 resultType, u32 image, u32 coordinates,
                     const SpirvImageOperands& operands);

    u32 opImageGather(u32 resultType, u32 sampledImage, u32 coordinates, u32 component,
                      const SpirvImageOperands& operands);

    u32 opImageDrefGather(u32 resultType, u32 sampledImage, u32 coordinates, u32 reference,
                          const SpirvImageOperands& operands);

    u32 opImageSampleImplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                 const SpirvImageOperands& operands);

    u32 opImageSampleExplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                 const SpirvImageOperands& operands);

    u32 opImageSampleProjImplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                     const SpirvImageOperands& operands);

    u32 opImageSampleProjExplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                     const SpirvImageOperands& operands);

    u32 opImageSampleDrefImplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                     u32 reference, const SpirvImageOperands& operands);

    u32 opImageSampleDrefExplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                     u32 reference, const SpirvImageOperands& operands);

    u32 opImageSampleProjDrefImplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                         u32 reference, const SpirvImageOperands& operands);

    u32 opImageSampleProjDrefExplicitLod(u32 resultType, u32 sampledImage, u32 coordinates,
                                         u32 reference, const SpirvImageOperands& operands);

    u32 opGroupNonUniformBallot(u32 resultType, u32 execution, u32 predicate);

    u32 opGroupNonUniformBallotBitCount(u32 resultType, u32 execution, u32 operation, u32 ballot);

    u32 opGroupNonUniformElect(u32 resultType, u32 execution);

    u32 opGroupNonUniformBroadcast(u32 resultType, u32 execution, u32 value, u32 id);

    u32 opGroupNonUniformBroadcastFirst(u32 resultType, u32 execution, u32 value);

    u32 opGroupNonUniformShuffle(u32 resultType, u32 execution, u32 value, u32 id);

    void opControlBarrier(u32 execution, u32 memory, u32 semantics);

    void opMemoryBarrier(u32 memory, u32 semantics);

    void opLoopMerge(u32 mergeBlock, u32 continueTarget, u32 loopControl);

    void opSelectionMerge(u32 mergeBlock, u32 selectionControl);

    void opBranch(u32 label);

    void opBranchConditional(u32 condition, u32 trueLabel, u32 falseLabel);

    void opSwitch(u32 selector, u32 jumpDefault, u32 caseCount,
                  const SpirvSwitchCaseLabel* caseLabels);

    u32 opPhi(u32 resultType, u32 sourceCount, const SpirvPhiLabel* sourceLabels);

    u32 opCopyObject(u32 resultType, u32 operand);

    void opCopyMemory(u32 targetId, u32 sourceId);

    u32 opSatConvertSToU(u32 resultType, u32 operand);

    u32 opSatConvertUToS(u32 resultType, u32 operand);

    void opReturn();

    void opKill();

    void opDemoteToHelperInvocation();

    void opEmitVertex(u32 streamId);

    void opEndPrimitive(u32 streamId);

    void opDebugPrintf(const char* format, u32 argCount, const u32* argList);

private:
    u32 m_version;
    u32 m_id = 1;
    u32 m_instExtGlsl450 = 0;
    u32 m_instExtNonSemantic = 0;

    SpirvCodeBuffer m_capabilities;
    SpirvCodeBuffer m_extensions;
    SpirvCodeBuffer m_instExt;
    SpirvCodeBuffer m_memoryModel;
    SpirvCodeBuffer m_entryPoints;
    SpirvCodeBuffer m_execModeInfo;
    SpirvCodeBuffer m_debugStrings;
    SpirvCodeBuffer m_debugNames;
    SpirvCodeBuffer m_annotations;
    SpirvCodeBuffer m_typeConstDefs;
    SpirvCodeBuffer m_variables;
    SpirvCodeBuffer m_code;

    std::unordered_set<u32> m_lateConsts;

    u32 defType(spv::Op op, u32 argCount, const u32* argIds);

    u32 defConst(spv::Op op, u32 typeId, u32 argumentCount, const u32* argumentList);

    void instImportGlsl450();
    void instImportNonSemantic();

    void enableDebugPrintf();

    u32 getImageOperandWordCount(const SpirvImageOperands& op) const;

    void putImageOperands(const SpirvImageOperands& op);
};

} // namespace Shader::Gcn
