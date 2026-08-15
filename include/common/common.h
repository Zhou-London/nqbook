#include <cstdint>
#include <deque>
namespace nq {

enum class Side : uint8_t { Buy = 0, Sell = 1 };

struct Order {
  char symbol[16];
  uint64_t id;
  uint64_t price;
  uint64_t qty;
  uint64_t event_time;
  Side side;
};

struct Trade {
  char symbol[16];
  uint64_t id;
  uint64_t price;
  uint64_t qty;
  uint64_t event_time;
  Side side;
};
} // namespace nq
