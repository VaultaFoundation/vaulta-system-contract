#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>

namespace system_token {
    using namespace eosio;

    struct [[eosio::table("accounts"), eosio::contract("system")]] account {
        asset    balance;
        bool     released = false;
        uint64_t primary_key()const { return balance.symbol.code().raw(); }
    };

    struct [[eosio::table("stat"), eosio::contract("system")]] currency_stats {
        asset    supply;
        asset    max_supply;
        name     issuer;

        uint64_t primary_key()const { return supply.symbol.code().raw(); }
    };

    typedef eosio::multi_index< "accounts"_n, account > accounts;
    typedef eosio::multi_index< "stat"_n, currency_stats > stats;


    // These are for interacting with the eosio.token contract, as the account structure
    // in this contract now differs to fix the RAM release bug.
    struct standard_account {
        asset    balance;
        uint64_t primary_key()const { return balance.symbol.code().raw(); }
    };
    typedef eosio::multi_index< "accounts"_n, standard_account > standard_accounts;
}