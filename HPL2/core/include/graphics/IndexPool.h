#pragma once

#include <cstdint>
#include <vector>

namespace hpl {
class IndexPool {
public:
  IndexPool(uint32_t reserve);

  uint32_t requestId();
  void returnId(uint32_t);
  void reset();
  void resetToReserved(uint32_t reserve);
private:
  struct IdRange {
    uint32_t m_start;
    uint32_t m_end;
  };
  std::vector<IdRange> m_avaliable;
};


} // namespace hpl
