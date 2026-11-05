#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace bmm {
struct SemanticResult { bool valid{}; double score{}; std::string model, version, explanation; };
class SemanticValidator {
public:
  virtual ~SemanticValidator() = default;
  virtual std::optional<SemanticResult> validate(std::string_view candidate,
      std::chrono::milliseconds timeout) = 0;
};
// No implementation is enabled in v0.2. A future Ollama adapter must use a bounded
// asynchronous shortlist queue, validate response schemas, cache by model+text,
// and never receive complete pages.
}
