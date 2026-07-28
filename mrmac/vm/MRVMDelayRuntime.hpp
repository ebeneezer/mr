#ifndef MRVM_DELAY_RUNTIME_HPP
#define MRVM_DELAY_RUNTIME_HPP

namespace mrvm_execution {

class DelayYield final {
 public:
	explicit DelayYield(int delayMillis) noexcept : millis(delayMillis) {
	}

	int millis;
};

bool sleepDelayBlocking(int millis);

} // namespace mrvm_execution

#endif
