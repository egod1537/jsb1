#include "sim/telemetry/TelemetryRegistry.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void TestPublishCreatesChannelAndStoresSamples() {
  telemetry::TelemetryRegistry telemetry;
  telemetry.Publish("aircraft/rates/r", 10.0, 0.12);
  telemetry.Publish("aircraft/rates/r", 10.01, 0.15);

  const telemetry::TelemetryChannel *channel =
      telemetry.Find("aircraft/rates/r");
  Require(channel != nullptr, "Publish did not create a telemetry channel");
  Require(channel->GetPath() == "aircraft/rates/r",
      "Telemetry channel did not retain its path");

  const auto &samples = channel->GetSamples();
  Require(samples.GetSize() == 2,
      "Telemetry channel did not retain both samples");
  Require(samples[0].timeSec == 10.0 && samples[0].value == 0.12,
      "First telemetry sample was incorrect");
  Require(samples[1].timeSec == 10.01 && samples[1].value == 0.15,
      "Second telemetry sample was incorrect");

  const telemetry::TelemetrySample *latest = channel->GetLatest();
  Require(latest != nullptr && latest->timeSec == 10.01
              && latest->value == 0.15,
      "Latest telemetry sample was incorrect");
}

void TestRingBufferOverwritesOldestSample() {
  telemetry::TelemetryRegistry telemetry(2);
  telemetry.Publish("aircraft/rates/r", 1.0, 10.0);
  telemetry.Publish("aircraft/rates/r", 2.0, 20.0);
  telemetry.Publish("aircraft/rates/r", 3.0, 30.0);

  const auto *channel = telemetry.Find("aircraft/rates/r");
  Require(channel != nullptr, "Bounded telemetry channel was not created");

  const auto &samples = channel->GetSamples();
  Require(samples.GetSize() == 2, "Telemetry history exceeded its capacity");
  Require(samples[0].timeSec == 2.0 && samples[0].value == 20.0,
      "Ring buffer did not overwrite the oldest sample");
  Require(samples[1].timeSec == 3.0 && samples[1].value == 30.0,
      "Ring buffer lost the newest sample");

  const auto publishedRange = telemetry.GetPublishedTimeRange();
  Require(publishedRange.has_value(),
      "Registry did not retain its published time range");
  Require(publishedRange->minSec == 1.0 && publishedRange->maxSec == 3.0,
      "Rolling channel storage truncated the registry time range");
}

void TestArchivedHistoryCanBeReadWithoutGrowingMemory() {
  telemetry::TelemetryRegistry telemetry(2);
  for (int sampleIndex = 1; sampleIndex <= 6; ++sampleIndex) {
    telemetry.Publish("aircraft/rates/r",
        static_cast<double>(sampleIndex),
        static_cast<double>(sampleIndex * 10));
  }

  const auto *channel = telemetry.Find("aircraft/rates/r");
  Require(channel != nullptr, "Archived telemetry channel was not created");
  Require(channel->GetSamples().GetSize() == 2,
      "Archived telemetry grew the in-memory ring buffer");
  Require(channel->GetArchivedSampleCount() == 4,
      "Evicted telemetry samples were not archived");

  const auto &allSamples = channel->ReadSamples(1.0, 6.0, 100);
  Require(allSamples.size() == 6,
      "Disk-backed range read did not restore the full history");
  for (std::size_t sampleIndex = 0; sampleIndex < allSamples.size();
      ++sampleIndex) {
    const double expectedTime = static_cast<double>(sampleIndex + 1);
    Require(allSamples[sampleIndex].timeSec == expectedTime,
        "Disk-backed range read returned samples out of order");
    Require(allSamples[sampleIndex].value == expectedTime * 10.0,
        "Disk-backed range read returned an incorrect value");
  }

  const auto &limitedSamples = channel->ReadSamples(1.0, 6.0, 3);
  Require(limitedSamples.size() == 3,
      "Long telemetry range was not limited for rendering");
  Require(limitedSamples.front().timeSec == 1.0
              && limitedSamples.back().timeSec == 6.0,
      "Range limiting discarded the telemetry endpoints");
}

void TestClosestSampleSearchIncludesArchivedHistory() {
  telemetry::TelemetryRegistry telemetry(2);
  telemetry.Publish("aircraft/rates/r", 1.0, 10.0);
  telemetry.Publish("aircraft/rates/r", 2.0, 20.0);
  telemetry.Publish("aircraft/rates/r", 3.0, 30.0);
  telemetry.Publish("aircraft/rates/r", 4.0, 40.0);

  const auto *channel = telemetry.Find("aircraft/rates/r");
  Require(channel != nullptr, "Closest-sample channel was not created");

  const auto archivedSample = channel->FindClosestSample(1.2);
  Require(archivedSample && archivedSample->timeSec == 1.0,
      "Closest-sample search did not inspect archived telemetry");
  const auto boundarySample = channel->FindClosestSample(2.6);
  Require(boundarySample && boundarySample->timeSec == 3.0,
      "Closest-sample search failed across the disk-memory boundary");
}

void TestRegistryInspectionAndClear() {
  telemetry::TelemetryRegistry telemetry;
  Require(!telemetry.GetPublishedTimeRange().has_value(),
      "Empty registry reported a published time range");
  telemetry.Publish("aircraft/rates/r", 1.0, 1.0);
  telemetry.Publish("aircraft/attitude/roll", 1.0, 2.0);

  const std::vector<std::string_view> paths = telemetry.GetChannelPaths();
  Require(paths.size() == 2, "Registry returned the wrong channel count");
  Require(paths[0] == "aircraft/attitude/roll"
              && paths[1] == "aircraft/rates/r",
      "Registry channel paths were incorrect");
  Require(telemetry.Find("missing/channel") == nullptr,
      "Registry found a channel that was never published");

  telemetry.Clear();
  Require(telemetry.GetChannelPaths().empty(),
      "Registry clear did not remove its channels");
  Require(telemetry.Find("aircraft/rates/r") == nullptr,
      "Registry clear left a published channel behind");
  Require(!telemetry.GetPublishedTimeRange().has_value(),
      "Registry clear retained the published time range");
}

void TestRegistryCreatesImmutableTransportContracts() {
  telemetry::TelemetryRegistry telemetry;
  telemetry.Publish("aircraft/rates/r", 1.0, 10.0);
  telemetry.Publish("aircraft/attitude/roll", 1.0, 20.0);
  telemetry.Publish("aircraft/rates/r", 2.0, 30.0);

  const telemetry::TelemetryFrame frame = telemetry.CaptureLatestFrame();
  Require(frame.available && frame.sequence == telemetry.GetVersion(),
      "Latest telemetry frame did not retain registry identity");
  Require(frame.timestamp == 2.0 && frame.values.size() == 2,
      "Latest telemetry frame had incorrect bounds or values");

  const telemetry::TelemetrySnapshot snapshot = telemetry.CaptureSnapshot();
  Require(snapshot.available && snapshot.series.size() == 2,
      "Telemetry snapshot did not capture all channels");
  const telemetry::TelemetrySeries *rates = snapshot.Find("aircraft/rates/r");
  Require(rates != nullptr && rates->samples.size() == 2,
      "Telemetry snapshot did not capture channel history");

  telemetry.Clear();
  Require(rates->samples.size() == 2,
      "Captured telemetry contract changed with its source registry");
}

void TestTelemetryRangeReadLimitsPlotData() {
  telemetry::TelemetrySeries series{.path = "test/long_plot"};
  constexpr std::size_t SourceSampleCount = 10'000;
  series.samples.reserve(SourceSampleCount);
  for (std::size_t index = 0; index < SourceSampleCount; ++index) {
    series.samples.push_back({
        .timeSec = static_cast<double>(index),
        .value = static_cast<double>(index),
    });
  }

  const std::vector<telemetry::TelemetrySample> visible =
      telemetry::ReadTelemetrySamples(series, 2000.0, 8000.0, 512);
  Require(visible.size() == 512,
      "Telemetry range read did not enforce the plot sample budget");
  Require(visible.front().timeSec == 2000.0 && visible.back().timeSec == 8000.0,
      "Telemetry range read did not retain visible endpoints");
}
} // namespace

int main() {
  try {
    TestPublishCreatesChannelAndStoresSamples();
    TestRingBufferOverwritesOldestSample();
    TestArchivedHistoryCanBeReadWithoutGrowingMemory();
    TestClosestSampleSearchIncludesArchivedHistory();
    TestRegistryInspectionAndClear();
    TestRegistryCreatesImmutableTransportContracts();
    TestTelemetryRangeReadLimitsPlotData();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
