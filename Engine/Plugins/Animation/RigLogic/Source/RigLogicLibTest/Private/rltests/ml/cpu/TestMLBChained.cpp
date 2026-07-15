// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef _MSC_VER
    #pragma warning(disable : 4503)
#endif

#include "rltests/Defs.h"
#include "rltests/StorageValueType.h"
#include "rltests/controls/ControlFixtures.h"
#include "rltests/ml/cpu/FixturesMLBChained.h"

#include "riglogic/TypeDefs.h"
#include "riglogic/ml/cpu/CPUMachineLearnedBehaviorFactory.h"
#include "riglogic/riglogic/RigMetadata.h"
#include "riglogic/system/simd/Utils.h"

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4324)
#endif

namespace {

// Exercises the chained topology: Gather -> MLP(odd layers) -> WeightedSum -> MLP(even layers) -> Scatter.
// The odd-layer intermediate MLP triggers the ping-pong normalization fix in Operation.h.
// Running multiple frames per LOD guards against stale-buffer regressions.
class MLBChainedInferenceTest : public ::testing::TestWithParam<rl4::CalculationType> {
protected:
    void SetUp() override {
        rl4::Configuration config = {};
        config.calculationType = GetParam();
        auto meta = rl4::RigMetadata::create(config, &reader, &memRes, rl4::InitializationMethod::Create);
        evaluator = rl4::ml::cpu::Factory::create(config, meta.get(), &reader, &memRes);
    }

protected:
    pma::AlignedMemoryResource memRes;
    rltests::ml::chained::CanonicalReader reader;
    rl4::MachineLearnedBehaviorEvaluator::Pointer evaluator;
};

}  // namespace

TEST_P(MLBChainedInferenceTest, InferencePerLOD) {
    using namespace rltests::ml::chained;

    auto inputInstanceFactory = ControlsFactory::getInstanceFactory(0,
                                                                    rawControlCount,
                                                                    0,
                                                                    mlControlCount,
                                                                    0);
    rl4::Vector<rl4::ControlInitializer> initialValues;
    auto inputInstance = inputInstanceFactory(initialValues, &this->memRes);
    auto inputBuffer = inputInstance->getInputBuffer();
    auto outputBuffer = inputBuffer.subview(rawControlCount, mlControlCount);
    std::copy(input::values.begin(), input::values.end(), inputBuffer.begin());

    auto intermediateOutputs = this->evaluator->createInstance(&this->memRes);
    for (std::uint16_t lod = 0u; lod < lodCount; ++lod) {
        // Run multiple frames per LOD to detect stale-buffer bugs.
        for (int frame = 0; frame < 3; ++frame) {
            std::fill(outputBuffer.begin(), outputBuffer.end(), 0.0f);
            this->evaluator->calculate(inputInstance.get(), intermediateOutputs.get(), lod);
            const auto& expected = output::valuesPerLOD[lod];
#ifdef RL_BUILD_WITH_HALF_FLOATS
            static constexpr float threshold = 0.15f;
#else
            static constexpr float threshold = 0.0002f;
#endif  // RL_BUILD_WITH_HALF_FLOATS
            ASSERT_ELEMENTS_NEAR(outputBuffer, expected, expected.size(), threshold);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(MLBChainedInferenceTestSuite,
                         MLBChainedInferenceTest,
                         ::testing::Values(rl4::CalculationType::AVX,
                                           rl4::CalculationType::SSE,
                                           rl4::CalculationType::NEON,
                                           rl4::CalculationType::Scalar));

#ifdef _MSC_VER
    #pragma warning(pop)
#endif
