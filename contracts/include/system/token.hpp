#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>

namespace eosio_token {
    using namespace eosio;

    // These are for interacting with the eosio.token contract, as the account structure
    // in this contract now differs to fix the RAM release bug.
    struct account {
        asset    balance;
        uint64_t primary_key()const { return balance.symbol.code().raw(); }
    };
    typedef eosio::multi_index< "accounts"_n, account > accounts;
}