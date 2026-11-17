#pragma once

#include <nq/feature_abi.h>

#include <Orderbook.h>

namespace nq {

nq_book_view MakeBookView(const Orderbook& book) noexcept;

}  // namespace nq
