#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>

namespace system_origin {
    using namespace eosio;

    static constexpr symbol EOS = symbol("EOS", 4);
    static constexpr symbol RAMCORE = symbol(symbol_code("RAMCORE"), 4);
    static constexpr symbol RAM = symbol(symbol_code("RAM"), 0);


    // NAME BIDS
    struct [[eosio::table]] bid_refund {
        name         bidder;
        asset        amount;

        uint64_t primary_key()const { return bidder.value; }
    };

    typedef eosio::multi_index< "bidrefunds"_n, bid_refund > bid_refund_table;


    // NEWACCOUNT
    struct permission_level_weight {
        permission_level  permission;
        uint16_t          weight;
    };

    struct key_weight {
        eosio::public_key  key;
        uint16_t           weight;
    };

    struct wait_weight {
        uint32_t           wait_sec;
        uint16_t           weight;
    };

    struct authority {
        uint32_t                              threshold = 0;
        std::vector<key_weight>               keys;
        std::vector<permission_level_weight>  accounts;
        std::vector<wait_weight>              waits;
    };

    struct [[eosio::table]] exchange_state {
        asset    supply;

        struct connector {
            asset balance;
            double weight = .5;
        };

        connector base;
        connector quote;

        uint64_t primary_key()const { return supply.symbol.raw(); }
    };

    typedef eosio::multi_index< "rammarket"_n, exchange_state > rammarket;

    int64_t get_bancor_input(int64_t out_reserve, int64_t inp_reserve, int64_t out){
        const double ob = out_reserve;
        const double ib = inp_reserve;
        int64_t inp = (ib * out) / (ob - out);
        if ( inp < 0 ) inp = 0;
        return inp;
    }

    int64_t get_bancor_output( int64_t inp_reserve, int64_t out_reserve, int64_t inp ){
        const double ib = inp_reserve;
        const double ob = out_reserve;
        const double in = inp;
        int64_t out = int64_t( (in * ob) / (ib + in) );
        if ( out < 0 ) out = 0;
        return out;
    }

    // DELEGATE BW / VOTING
    struct [[eosio::table, eosio::contract("eosio.system")]] refund_request {
        name            owner;
        time_point_sec  request_time;
        eosio::asset    net_amount;
        eosio::asset    cpu_amount;

        uint64_t  primary_key()const { return owner.value; }
    };

    typedef eosio::multi_index< "refunds"_n, refund_request > refunds_table;
}