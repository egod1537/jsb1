#include "common/crypto/Sha256.hpp"
#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/gnc/autopilot/MyAutopilot.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"
#include "sim/scenario/SimulationScenarioSerializer.hpp"
#include "telemetry/aircraft_state.pb.h"
#include "telemetry/control.pb.h"

#include <google/protobuf/descriptor.h>

#include <cassert>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#ifndef JSB_TEST_CONTRACT_SCENARIO_PATH
#define JSB_TEST_CONTRACT_SCENARIO_PATH                                        \
  "contract/examples/scenario/roll_hold.yaml"
#endif

#ifndef JSB_TEST_EXECUTION_VARIANTS_PATH
#define JSB_TEST_EXECUTION_VARIANTS_PATH "contract/execution/variants.json"
#endif

#ifndef JSB_TEST_EXECUTION_CAPABILITIES_PATH
#define JSB_TEST_EXECUTION_CAPABILITIES_PATH                                   \
  "contract/execution/capabilities.json"
#endif

namespace {
void TestCanonicalScenarioIsExecutableInput() {
  sim::SimulationScenario scenario;
  std::string error;
  assert(
      sim::SimulationScenarioSerializer::Load(JSB_TEST_CONTRACT_SCENARIO_PATH,
          scenario,
          error));
  assert(error.empty());
  assert(scenario.schemaVersion == 1);
  assert(scenario.scenarioType == "roll_hold");
  assert(scenario.aircraft == "c172x");
  const std::string serialized =
      sim::SimulationScenarioSerializer::Serialize(scenario);
  assert(serialized.find("autopilot") == std::string::npos);
}

void TestUnsupportedScenarioVersionIsRejected() {
  sim::SimulationScenario scenario;
  scenario.schemaVersion = 2;
  std::string error;
  assert(!sim::ValidateSimulationScenario(scenario, &error));
  assert(error.find("schema_version") != std::string::npos);
}

void TestAuthoritativeScenarioValidation() {
  sim::SimulationScenario valid;
  std::string error;
  const auto requireInvalid = [&error](sim::SimulationScenario scenario,
                                  std::string_view path) {
    assert(!sim::ValidateSimulationScenario(scenario, &error));
    assert(error.find(path) != std::string::npos);
  };

  sim::SimulationScenario invalid = valid;
  invalid.aircraft = "unknown";
  requireInvalid(invalid, "aircraft");
  invalid = valid;
  invalid.durationSec = 0.0;
  requireInvalid(invalid, "simulation.duration_sec");
  invalid = valid;
  invalid.initialCondition.airspeedKts =
      std::numeric_limits<double>::quiet_NaN();
  requireInvalid(invalid, "initial_condition.airspeed_kts");
  invalid = valid;
  invalid.events.front().timeSec = valid.durationSec + 1.0;
  requireInvalid(invalid, "events[0].time_sec");
  invalid = valid;
  invalid.events.front().command.type =
      static_cast<sim::ScenarioCommandType>(99);
  requireInvalid(invalid, "events[0].command.type");

  const std::string serialized =
      sim::SimulationScenarioSerializer::Serialize(valid);
  sim::SimulationScenario parsed;
  assert(sim::SimulationScenarioSerializer::Deserialize(serialized,
      parsed,
      error));

  const std::string invalidAutopilot = "autopilot: unsupported\n" + serialized;
  assert(!sim::SimulationScenarioSerializer::Deserialize(invalidAutopilot,
      parsed,
      error));
  assert(error.find("autopilot") != std::string::npos);
}

void TestExecutionVariantContractAndResolution() {
  sim::ExecutionVariant variant = sim::ExecutionVariant::Primary;
  assert(sim::TryParseExecutionVariant("baseline", variant));
  assert(variant == sim::ExecutionVariant::Baseline);
  assert(sim::TryParseExecutionVariant("primary", variant));
  assert(variant == sim::ExecutionVariant::Primary);
  assert(!sim::TryParseExecutionVariant("Primary", variant));
  assert(!sim::TryParseExecutionVariant("foo", variant));
  assert(sim::SupportedExecutionVariants.size() == 2);
  assert(sim::ToString(sim::SupportedExecutionVariants[0]) == "baseline");
  assert(sim::ToString(sim::SupportedExecutionVariants[1]) == "primary");

  const auto baseline = sim::ExecutionVariantResolver::CreateAutopilot(
      sim::ExecutionVariant::Baseline);
  const auto primary = sim::ExecutionVariantResolver::CreateAutopilot(
      sim::ExecutionVariant::Primary);
  assert(dynamic_cast<gnc::PX4Autopilot *>(baseline.get()) != nullptr);
  assert(dynamic_cast<gnc::MyAutopilot *>(primary.get()) != nullptr);

  std::ifstream capabilityInput(JSB_TEST_EXECUTION_VARIANTS_PATH,
      std::ios::binary);
  assert(capabilityInput);
  const std::string capabilities(
      std::istreambuf_iterator<char>(capabilityInput),
      {});
  assert(capabilities.find("\"baseline\"") != std::string::npos);
  assert(capabilities.find("\"primary\"") != std::string::npos);

  std::ifstream executionCapabilitiesInput(JSB_TEST_EXECUTION_CAPABILITIES_PATH,
      std::ios::binary);
  assert(executionCapabilitiesInput);
  const std::string executionCapabilities(
      std::istreambuf_iterator<char>(executionCapabilitiesInput),
      {});
  assert(executionCapabilities.find("\"compare\"") != std::string::npos);
  assert(executionCapabilities.find("\"baseline\"") != std::string::npos);
  assert(executionCapabilities.find("\"primary\"") != std::string::npos);
}

void TestLegacyAutopilotMigration() {
  const std::string canonical =
      sim::SimulationScenarioSerializer::Serialize(sim::SimulationScenario{});
  for (const std::string legacyField : {
           std::string("autopilot: baseline\n"),
           std::string("autopilot:\n  type: baseline\n"),
       }) {
    sim::SimulationScenario scenario;
    sim::ScenarioLoadMetadata metadata;
    std::string error;
    assert(
        sim::SimulationScenarioSerializer::Deserialize(legacyField + canonical,
            scenario,
            error,
            &metadata));
    assert(metadata.legacyVariant == sim::ExecutionVariant::Baseline);
    assert(!metadata.warnings.empty());
    assert(
        sim::SimulationScenarioSerializer::Serialize(scenario).find("autopilot")
        == std::string::npos);
  }
}

void TestRequiredProtobufSignalsExist() {
  const google::protobuf::Descriptor *aircraft =
      jsb::telemetry::v1::AircraftState::descriptor();
  assert(aircraft->FindFieldByName("sim_time_ns") != nullptr);
  assert(aircraft->FindFieldByName("roll_rad") != nullptr);
  assert(aircraft->FindFieldByName("roll_rate_rad_s") != nullptr);

  const google::protobuf::Descriptor *control =
      jsb::telemetry::v1::RollControlState::descriptor();
  assert(control->FindFieldByName("commanded_roll_rad") != nullptr);
  assert(control->FindFieldByName("commanded_roll_rate_rad_s") != nullptr);
  assert(control->FindFieldByName("aileron_command") != nullptr);
}

void TestScenarioDigestUsesSha256() {
  assert(common::crypto::Sha256Hex("abc")
         == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
} // namespace

int main() {
  TestCanonicalScenarioIsExecutableInput();
  TestUnsupportedScenarioVersionIsRejected();
  TestAuthoritativeScenarioValidation();
  TestExecutionVariantContractAndResolution();
  TestLegacyAutopilotMigration();
  TestRequiredProtobufSignalsExist();
  TestScenarioDigestUsesSha256();
  return 0;
}
