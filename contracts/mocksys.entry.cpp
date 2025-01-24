#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <system/oldsystem.hpp>
using namespace eosio;
using namespace system_origin;

CONTRACT mocksys : public contract {
    public:
        using contract::contract;

        static constexpr symbol EOS = symbol("EOS", 4);

        struct [[eosio::table]] rex_pool {
            uint8_t    version = 0;
            asset      total_lent;
            asset      total_unlent;
            asset      total_rent;
            asset      total_lendable;
            asset      total_rex;
            asset      namebid_proceeds;
            uint64_t   loan_num = 0;

            uint64_t primary_key()const { return 0; }
        };

        typedef eosio::multi_index< "rexpool"_n, rex_pool > rex_pool_table;

        struct pair_time_point_sec_int64 {
            time_point_sec first;
            int64_t        second;
        };

        struct [[eosio::table,eosio::contract("eosio.system")]] rex_fund {
            uint8_t version = 0;
            name    owner;
            asset   balance;

            uint64_t primary_key()const { return owner.value; }
        };

        typedef eosio::multi_index< "rexfund"_n, rex_fund > rex_fund_table;

        struct [[eosio::table]] rex_balance {
            uint8_t version = 0;
            name    owner;
            asset   vote_stake;
            asset   rex_balance;
            int64_t matured_rex = 0;
            std::vector<pair_time_point_sec_int64> rex_maturities;

            uint64_t primary_key()const { return owner.value; }
        };

        typedef eosio::multi_index< "rexbal"_n, rex_balance > rex_balance_table;

        TABLE last_action {
            name action;
        };

        typedef eosio::singleton<"lastaction"_n, last_action> last_action_table;

        void set_last_action(const name& action) {
            last_action_table _last_action(get_self(), get_self().value);
            _last_action.set(last_action{action}, get_self());
        }


        ACTION bidname( const name& bidder, const name& newname, const asset& bid ){
            require_auth( bidder );
            check( bid.symbol == EOS, "asset must be system token" );
            check( bid.amount > 0, "insufficient bid" );

            action(
                permission_level{bidder, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(bidder, get_self(), bid, std::string(""))
            ).send();

            set_last_action("bidname"_n);
        }

        struct [[eosio::table]] bid_refund {
            name         bidder;
            asset        amount;

            uint64_t primary_key()const { return bidder.value; }
        };

        typedef eosio::multi_index< "bidrefunds"_n, bid_refund > bid_refund_table;

        ACTION insertrefund( const name& bidder, const name& newname ){
            bid_refund_table refunds( get_self(), newname.value );
            refunds.emplace( get_self(), [&]( auto& refund ){
                refund.bidder = bidder;
                refund.amount = asset(1'0000, EOS);
            });
        }

        ACTION bidrefund( const name& bidder, const name& newname ){
            asset bid(1'0000, EOS);
            action(
                permission_level{get_self(), "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(get_self(), bidder, bid, std::string(""))
            ).send();

            set_last_action("bidrefund"_n);
        }

        ACTION buyram( const name& payer, const name& receiver, const asset& quantity ){
            action(
                permission_level{payer, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(payer, get_self(), quantity, std::string(""))
            ).send();

            set_last_action("buyram"_n);
        }

        ACTION buyramself( const name& payer, const asset& quantity ){
            action(
                permission_level{payer, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(payer, get_self(), quantity, std::string(""))
            ).send();

            set_last_action("buyramself"_n);
        }

        ACTION buyramburn( const name& payer, const asset& quantity, const std::string& memo ){
            action(
                permission_level{payer, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(payer, get_self(), quantity, memo)
            ).send();

            set_last_action("buyramburn"_n);
        }

        ACTION setrammarket( const asset& ram, const asset& eos ){
            rammarket _rammarket(get_self(), get_self().value);
            auto itr = _rammarket.find(RAMCORE.raw());

            check( ram.symbol == RAM, "ram symbol must be RAM" );
            check( eos.symbol == EOS, "eos symbol must be EOS" );

            if( itr == _rammarket.end() ){
                _rammarket.emplace( get_self(), [&]( auto& rm ){
                    rm.supply = asset(10000000000'0000, RAMCORE);
                    rm.base.balance = ram;
                    rm.quote.balance = eos;
                });
            } else {
                _rammarket.modify( itr, get_self(), [&]( auto& rm ){
                    rm.base.balance.amount = ram.amount;
                    rm.quote.balance.amount = eos.amount;
                });
            }
        }

        ACTION buyrambytes( name payer, name receiver, uint32_t bytes ){
            rammarket _rammarket(get_self(), get_self().value);
            auto itr = _rammarket.find(RAMCORE.raw());
            const int64_t ram_reserve   = itr->base.balance.amount;
            const int64_t eos_reserve   = itr->quote.balance.amount;
            const int64_t cost          = get_bancor_input( ram_reserve, eos_reserve, bytes );
            const int64_t cost_plus_fee = cost / double(0.995);
            const asset eos_quantity    = asset(cost_plus_fee, EOS);

            action(
                permission_level{payer, "active"_n},
                get_self(),
                "buyram"_n,
                std::make_tuple(payer, receiver, eos_quantity)
            ).send();

            set_last_action("buyrambytes"_n);
        }

        ACTION ramburn( const name& owner, const uint32_t& bytes, const std::string& memo ){
            set_last_action("ramburn"_n);
        }

        ACTION ramtransfer( const name& from, const name& to, const uint32_t& bytes, const std::string& memo ){
            set_last_action("ramtransfer"_n);
        }

        asset ram_to_tokens( const asset& from, const symbol& to ){
            rammarket _rammarket("eosio"_n, "eosio"_n.value);
            auto itr = _rammarket.find(RAMCORE.raw());

            const auto& base_symbol  = itr->base.balance.symbol;
            const auto& quote_symbol = itr->quote.balance.symbol;
            check( from.symbol != to, "cannot convert to the same symbol" );

            asset out( 0, to );
            if ( from.symbol == base_symbol && to == quote_symbol ) {
                out.amount = get_bancor_output( itr->base.balance.amount, itr->quote.balance.amount, from.amount );
            } else if ( from.symbol == quote_symbol && to == base_symbol ) {
                out.amount = get_bancor_output( itr->quote.balance.amount, itr->base.balance.amount, from.amount );
            } else {
                check( false, "invalid conversion" );
            }
            return out;
        }

        ACTION sellram( const name& account, const uint32_t& bytes ){
            asset eos_quantity = ram_to_tokens( asset(bytes, RAM), EOS );

            action(
                permission_level{get_self(), "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(get_self(), account, eos_quantity, std::string(""))
            ).send();

            set_last_action("sellram"_n);
        }


        TABLE mockfund {
            uint8_t version = 0;
            name    owner;
            asset   balance;

            uint64_t primary_key()const { return owner.value; }
        };

        typedef eosio::multi_index< "rexfund"_n, mockfund > mock_rex_fund_table;


        ACTION deposit( const name& owner, const asset& amount ){
            require_auth( owner );

            check(amount.symbol == EOS, "asset must be system token" );
            check(amount.amount > 0, "insufficient deposit" );

            action(
                permission_level{owner, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(owner, get_self(), amount, std::string(""))
            ).send();

            rex_fund_table deposits( get_self(), get_self().value );
            auto itr = deposits.find( owner.value );

            if( itr == deposits.end() ){
                deposits.emplace( owner, [&]( auto& dep ){
                    dep.owner = owner;
                    dep.balance = amount;
                });
            } else {
                deposits.modify( itr, owner, [&]( auto& dep ){
                    dep.balance += amount;
                });
            }

            set_last_action("deposit"_n);
        }

        TABLE staked {
            name account;
            asset balance;

            uint64_t primary_key()const { return account.value; }
        };

        typedef eosio::multi_index< "staked"_n, staked > staked_table;

        asset eos_to_rex( const asset& amount ){
            rex_pool_table pool(get_self(), get_self().value);
            auto it = pool.begin();
            check(it != pool.end(), "REX pool not found");

            uint64_t S0 = it->total_lendable.amount;
            uint64_t S1 = S0 + amount.amount;
            uint64_t R0 = it->total_rex.amount;
            uint64_t R1 = R0 + (uint64_t)((double)it->total_rex.amount * (double)amount.amount / (double)it->total_lendable.amount);

            return asset(R1 - R0, symbol("REX", 4));
        }

        asset rex_to_eos(int64_t rex){
            rex_pool_table pool(get_self(), get_self().value);
            auto it = pool.begin();
            check(it != pool.end(), "REX pool not found");

            uint64_t S0 = it->total_lendable.amount;
            uint64_t R0 = it->total_rex.amount;
            uint64_t R1 = R0 + rex;
            uint64_t S1 = (uint64_t)((double)S0 * (double)R1 / (double)R0);

            return asset(S1 - S0, EOS);
        }

        ACTION buyrex( const name& from, const asset& amount ){
            require_auth( from );

            rex_fund_table deposits( get_self(), get_self().value );
            auto itr = deposits.find( from.value );

            check( itr != deposits.end(), "no deposit found" );

            check( itr->balance >= amount, "insufficient balance" );

            deposits.modify( itr, from, [&]( auto& dep ){
                dep.balance -= amount;
            });

            asset rex = eos_to_rex( amount );
            staked_table _staked( get_self(), get_self().value );
            auto itr_staked = _staked.find( from.value );

            if( itr_staked == _staked.end() ){
                _staked.emplace( from, [&]( auto& dep ){
                    dep.account = from;
                    dep.balance = rex;
                });
            } else {
                _staked.modify( itr_staked, from, [&]( auto& dep ){
                    dep.balance += rex;
                });
            }


            set_last_action("buyrex"_n);
        }

        ACTION setrex(
            const asset& total_lendable,
            const asset& total_rex
        ){
            rex_pool_table rexpool( get_self(), get_self().value );
            auto itr = rexpool.find( 0 );

            if( itr == rexpool.end() ){
                rexpool.emplace( get_self(), [&]( auto& pool ){
                    pool.total_lendable = total_lendable;
                    pool.total_rex = total_rex;
                });
            } else {
                rexpool.modify( itr, get_self(), [&]( auto& pool ){
                    pool.total_lendable = total_lendable;
                    pool.total_rex = total_rex;
                });
            }
        }

        TABLE unstaking {
            name account;
            asset balance;

            uint64_t primary_key()const { return account.value; }
        };

        typedef eosio::multi_index< "unstaking"_n, unstaking > unstaking_table;

        ACTION mvfrsavings( const name& owner, const asset& rex ){
            require_auth( owner );

            staked_table _staked( get_self(), get_self().value );
            auto itr = _staked.find( owner.value );

            check( itr != _staked.end(), "no staked found" );
            check( itr->balance >= rex, "insufficient balance" );

            _staked.modify( itr, owner, [&]( auto& dep ){
                dep.balance -= rex;
            });

            unstaking_table _unstaking( get_self(), get_self().value );
            auto itr_unstaking = _unstaking.find( owner.value );

            if( itr_unstaking == _unstaking.end() ){
                _unstaking.emplace( owner, [&]( auto& dep ){
                    dep.account = owner;
                    dep.balance = rex;
                });
            } else {
                _unstaking.modify( itr_unstaking, owner, [&]( auto& dep ){
                    dep.balance += rex;
                });
            }


            rex_balance_table _rex_balance( get_self(), get_self().value );
            auto itr_rex_balance = _rex_balance.find( owner.value );

            if( itr_rex_balance == _rex_balance.end() ){
                _rex_balance.emplace( owner, [&]( auto& dep ){
                    dep.owner = owner;
                    dep.matured_rex = rex.amount;
                });
            } else {
                _rex_balance.modify( itr_rex_balance, owner, [&]( auto& dep ){
                    dep.matured_rex += rex.amount;
                });
            }

            set_last_action("mvfrsavings"_n);
        }

        ACTION sellrex( const name& owner, const asset& rex ){
            require_auth( owner );

            // sell rex from unstaking table
            unstaking_table _unstaking( get_self(), get_self().value );
            auto itr = _unstaking.find( owner.value );

            check( itr != _unstaking.end(), "no unstaking found" );
            check( itr->balance >= rex, "insufficient balance" );

            _unstaking.modify( itr, owner, [&]( auto& dep ){
                dep.balance -= rex;
            });

            asset eos = rex_to_eos( rex.amount );

            // put EOS back into deposited table
            rex_fund_table deposits( get_self(), get_self().value );
            auto itr_deposits = deposits.find( owner.value );

            if( itr_deposits == deposits.end() ){
                deposits.emplace( owner, [&]( auto& dep ){
                    dep.owner = owner;
                    dep.balance = eos;
                });
            } else {
                deposits.modify( itr_deposits, owner, [&]( auto& dep ){
                    dep.balance += eos;
                });
            }

            set_last_action("sellrex"_n);
        }

        ACTION withdraw( const name& owner, const asset& quantity ){
            require_auth( owner );

            // send EOS to owner from deposited table
            rex_fund_table deposits( get_self(), get_self().value );
            auto itr = deposits.find( owner.value );

            check( itr != deposits.end(), "no deposit found" );
            check( itr->balance >= quantity, "insufficient balance" );

            deposits.modify( itr, owner, [&]( auto& dep ){
                dep.balance -= quantity;
            });

            action(
                permission_level{get_self(), "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(get_self(), owner, quantity, std::string(""))
            ).send();

            set_last_action("withdraw"_n);
        }

        ACTION powerup( const name& payer, const name& receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac, const asset& max_payment ){
            require_auth( payer );
            check( max_payment.symbol == EOS, "asset must be system token" );
            check( max_payment.amount > 0, "insufficient payment" );

            // some custom logic for tests so I can check what happens when max_payment isn't fully taken
            if (max_payment.amount > 1'0000) {
                asset quantity(1'0000, EOS);
                action(
                    permission_level{payer, "active"_n},
                    "eosio.token"_n,
                    "transfer"_n,
                    std::make_tuple(payer, get_self(), quantity, std::string(""))
                ).send();
            } else {
                action(
                    permission_level{payer, "active"_n},
                    "eosio.token"_n,
                    "transfer"_n,
                    std::make_tuple(payer, get_self(), max_payment, std::string(""))
                ).send();
            }


            set_last_action("powerup"_n);
        }

        TABLE stakes {
            name account;
            asset net_weight;
            asset cpu_weight;

            uint64_t primary_key()const { return account.value; }
        };

        typedef eosio::multi_index< "stakes"_n, stakes > stakes_table;

        typedef eosio::multi_index< "refunds"_n, refund_request > refunds_table;

        TABLE votes {
            name voter;
            std::vector<name> producers;

            uint64_t primary_key()const { return voter.value; }
        };

        typedef eosio::multi_index< "votes"_n, votes > votes_table;

        ACTION delegatebw( const name& from, const name& receiver, const asset& stake_net_quantity, const asset& stake_cpu_quantity, bool transfer ){
            require_auth( from );
            check( stake_net_quantity.symbol == EOS, "net asset must be system token" );
            check( stake_cpu_quantity.symbol == EOS, "cpu asset must be system token" );
            check( stake_net_quantity.amount >= 0, "must stake net a positive amount" );
            check( stake_cpu_quantity.amount >= 0, "must stake cpu a positive amount" );
            check( stake_net_quantity.amount + stake_cpu_quantity.amount > 0, "must stake a positive amount" );

            stakes_table _stakes( get_self(), get_self().value );
            auto itr = _stakes.find( from.value );

            if( itr == _stakes.end() ){
                _stakes.emplace( from, [&]( auto& s ){
                    s.account = from;
                    s.net_weight = stake_net_quantity;
                    s.cpu_weight = stake_cpu_quantity;
                });
            } else {
                _stakes.modify( itr, from, [&]( auto& s ){
                    s.net_weight += stake_net_quantity;
                    s.cpu_weight += stake_cpu_quantity;
                });
            }


            action(
                permission_level{from, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(from, get_self(), stake_net_quantity + stake_cpu_quantity, std::string(""))
            ).send();

            set_last_action("delegatebw"_n);
        }

        ACTION undelegatebw( const name& from, const name& receiver, const asset& unstake_net_quantity, const asset& unstake_cpu_quantity ){
            require_auth( from );
            check( unstake_net_quantity.symbol == EOS, "net asset must be system token" );
            check( unstake_cpu_quantity.symbol == EOS, "cpu asset must be system token" );
            check( unstake_net_quantity.amount >= 0, "must unstake net a positive amount" );
            check( unstake_cpu_quantity.amount >= 0, "must unstake cpu a positive amount" );
            check( unstake_net_quantity.amount + unstake_cpu_quantity.amount > 0, "must unstake a positive amount" );

            stakes_table _stakes( get_self(), get_self().value );
            auto itr = _stakes.find( from.value );

            check( itr != _stakes.end(), "no stake found" );
            check( itr->net_weight >= unstake_net_quantity, "insufficient net stake" );
            check( itr->cpu_weight >= unstake_cpu_quantity, "insufficient cpu stake" );

            _stakes.modify( itr, from, [&]( auto& s ){
                s.net_weight -= unstake_net_quantity;
                s.cpu_weight -= unstake_cpu_quantity;
            });

            refunds_table _refunds( get_self(), get_self().value );
            auto itr_refunds = _refunds.find( from.value );

            if( itr_refunds == _refunds.end() ){
                _refunds.emplace( from, [&]( auto& r ){
                    r.owner = from;
                    r.request_time = current_time_point();
                    r.net_amount = unstake_net_quantity;
                    r.cpu_amount = unstake_cpu_quantity;

                });
            } else {
                _refunds.modify( itr_refunds, from, [&]( auto& r ){
                    r.request_time = current_time_point();
                    r.net_amount += unstake_net_quantity;
                    r.cpu_amount += unstake_cpu_quantity;
                });
            }

            set_last_action("undelegatebw"_n);
        }

        ACTION refund( const name& owner ){
            require_auth( owner );

            refunds_table _refunds( get_self(), get_self().value );
            auto itr = _refunds.find( owner.value );

            check( itr != _refunds.end(), "no refund found" );

            action(
                permission_level{get_self(), "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(get_self(), owner, itr->net_amount + itr->cpu_amount, std::string(""))
            ).send();

            _refunds.erase( itr );

            set_last_action("refund"_n);
        }

        ACTION voteproducer( const name& voter, const std::vector<name>& producers ){
            require_auth( voter );

            votes_table _votes( get_self(), get_self().value );
            auto itr = _votes.find( voter.value );

            if( itr == _votes.end() ){
                _votes.emplace( voter, [&]( auto& v ){
                    v.voter = voter;
                    v.producers = producers;
                });
            } else {
                _votes.modify( itr, voter, [&]( auto& v ){
                    v.producers = producers;
                });
            }

            set_last_action("voteproducer"_n);
        }
};
