#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <system/token.hpp>
#include <system/oldsystem.hpp>
using namespace eosio;
using namespace system_token;
using namespace system_origin;

class [[eosio::contract("system")]] system_contract : public contract {
    public:
        using contract::contract;

        TABLE config {
            symbol token_symbol;
        };

        typedef eosio::singleton<"config"_n, config> config_table;

        /**
        * Initialize the token with a maximum supply and given token ticker and store a ref to which ticker is selected.
        * This also issues the maximum supply to the system contract itself so that it can use it for
        * swaps.
        * @param maximum_supply - The maximum supply of the token and the symbol of the new token.
        */
        ACTION init(asset maximum_supply) {
            require_auth(get_self());
            config_table _config(get_self(), get_self().value);
            check(!_config.exists(), "This system contract is already initialized");

            auto sym = maximum_supply.symbol;
            check( maximum_supply.is_valid(), "invalid supply");
            check( maximum_supply.amount > 0, "max-supply must be positive");

            _config.set(config{
                .token_symbol = sym
            }, get_self());

            stats statstable( get_self(), sym.code().raw() );
            statstable.emplace( get_self(), [&]( auto& s ) {
               s.supply = maximum_supply;
               s.max_supply    = maximum_supply;
               s.issuer        = get_self();
            });

            add_balance( get_self(), maximum_supply, get_self() );
        }


        // ----------------------------------------------------
        // SYSTEM TOKEN ---------------------------------------
        // ----------------------------------------------------

        ACTION transfer(const name& from, const name& to, const asset& quantity, const std::string& memo) {
            check( from != to, "cannot transfer to self" );
            require_auth( from );
            check( is_account( to ), "to account does not exist");

            auto sym = quantity.symbol.code();
            stats statstable( get_self(), sym.raw() );
            const auto& st = statstable.get( sym.raw() );

            check( quantity.is_valid(), "invalid quantity" );
            check( quantity.amount > 0, "must transfer positive quantity" );
            check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
            check( memo.size() <= 256, "memo has more than 256 bytes" );

            auto payer = has_auth( to ) ? to : from;

            sub_balance( from, quantity );
            add_balance( to, quantity, payer );

            require_recipient( from );
            require_recipient( to );

            // If `from` is sending XYZ tokens to this contract
            // they are swapping from XYZ to EOS
            if(to == get_self()) {
                check(quantity.symbol == get_token_symbol(), "Wrong token used");
                credit_eos_to(from, quantity);
            }
        }

        ACTION open( const name& owner, const symbol& symbol, const name& ram_payer ) {
            require_auth( ram_payer );

            check( is_account( owner ), "owner account does not exist" );

            auto sym_code_raw = symbol.code().raw();
            stats statstable( get_self(), sym_code_raw );
            const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
            check( st.supply.symbol == symbol, "symbol precision mismatch" );

            accounts acnts( get_self(), owner.value );
            auto it = acnts.find( sym_code_raw );
            if( it == acnts.end() ) {
                acnts.emplace( ram_payer, [&]( auto& a ){
                    a.balance = asset{0, symbol};
                });
            }
        }

        ACTION close( const name& owner, const symbol& symbol ) {
            require_auth( owner );
            accounts acnts( get_self(), owner.value );
            auto it = acnts.find( symbol.code().raw() );
            check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
            check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
            acnts.erase( it );
        }

        ACTION retire( const name& owner, const asset& quantity, const std::string& memo ) {
            auto sym = quantity.symbol;
            check( sym.is_valid(), "invalid symbol name" );
            check( memo.size() <= 256, "memo has more than 256 bytes" );

            stats statstable( get_self(), sym.code().raw() );
            auto existing = statstable.find( sym.code().raw() );
            check( existing != statstable.end(), "token with symbol does not exist" );
            const auto& st = *existing;

            require_auth( st.issuer );
            check( quantity.is_valid(), "invalid quantity" );
            check( quantity.amount > 0, "must retire positive quantity" );

            check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

            statstable.modify( st, same_payer, [&]( auto& s ) {
               s.supply -= quantity;
            });

            sub_balance( st.issuer, quantity );
        }

        void add_balance( const name& owner, const asset& value, const name& ram_payer ) {
            accounts to_acnts( get_self(), owner.value );
            auto to = to_acnts.find( value.symbol.code().raw() );
            if( to == to_acnts.end() ) {
                to_acnts.emplace( ram_payer, [&]( auto& a ){
                    a.balance = value;
                });
            } else {
                to_acnts.modify( to, same_payer, [&]( auto& a ) {
                    a.balance += value;
                });
            }
        }

        void sub_balance( const name& owner, const asset& value ) {
            accounts from_acnts( get_self(), owner.value );

            const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
            check( from.balance.amount >= value.amount, "overdrawn balance" );

            from_acnts.modify( from, owner, [&]( auto& a ) {
                a.balance -= value;
            });
        }

        // ----------------------------------------------------
        // SWAP -----------------------------------------------
        // ----------------------------------------------------

        // When this contract receives EOS tokens, it will swap them for XYZ tokens and credit them to the sender.
        [[eosio::on_notify("eosio.token::transfer")]]
        void on_transfer(const name& from, const name& to, const asset& quantity, const std::string& memo) {
            if (from == get_self() || to != get_self()) return;
            check(quantity.amount > 0, "Swap amount must be greater than 0");

            // Ignore for system accounts, otherwise when unstaking or selling ram this will swap EOS for
            // XYZ and credit them to the sending account which will lock those tokens.
            if(from == "eosio.ram"_n) return;
            if(from == "eosio.stake"_n) return;

            check(quantity.symbol == EOS, "Invalid symbol");
            asset swap_amount = asset(quantity.amount, get_token_symbol());
            action(
                permission_level{get_self(), "active"_n},
                get_self(),
                "transfer"_n,
                std::make_tuple(get_self(), from, swap_amount, std::string(""))
            ).send();
        }

        // allow account owners to disallow the `swapto` action with their account as destination.
        // This has been requested by exchanges who prefer to receive funds into their hot wallets 
        // exclusively via the root `transfer` action.
        TABLE blocked_recipient {
            name account;

            uint64_t primary_key() const { return account.value; }
        };

        typedef eosio::multi_index<"blocked"_n, blocked_recipient> blocked_table;

        // Allows an account to block themselves from being a recipient of the `swapto` action.
        ACTION blockswapto(const name& account, const bool block) {
            // The account owner or this contract can block or unblock an account.
            if(!has_auth(get_self())) {
                require_auth(account);
            }

            blocked_table _blocked(get_self(), get_self().value);
            auto itr = _blocked.find(account.value);
            if(block) {
                if(itr == _blocked.end()) {
                    _blocked.emplace(account, [&](auto& b) {
                        b.account = account;
                    });
                }
            } else {
                if(itr != _blocked.end()) {
                    _blocked.erase(itr);
                }
            }
        }
        

        // This action allows exchanges to support "swap & withdraw" for their users and have the swapped tokens flow
        // to the users instead of to their own hot wallets.
        ACTION swapto(const name& from, const name& to, const asset& quantity, const std::string& memo) {
            require_auth(from);

            blocked_table _blocked(get_self(), get_self().value);
            auto itr = _blocked.find(to.value);
            check(itr == _blocked.end(), "Recipient is blocked from receiving swapped tokens: " + to.to_string());

            if(quantity.symbol == EOS){
                // First swap the EOS to XYZ and credit it to the user
                action(
                    permission_level{from, "active"_n},
                    "eosio.token"_n,
                    "transfer"_n,
                    std::make_tuple(from, get_self(), asset(quantity.amount, EOS), memo)
                ).send();

                // Then transfer the swapped XYZ to the target account
                action(
                    permission_level{from, "active"_n},
                    get_self(),
                    "transfer"_n,
                    std::make_tuple(from, to, asset(quantity.amount, get_token_symbol()), memo)
                ).send();
            } else if (quantity.symbol == get_token_symbol()) {
                // First swap the XYZ to EOS and credit it to the user
                action(
                    permission_level{from, "active"_n},
                    get_self(),
                    "transfer"_n,
                    std::make_tuple(from, get_self(), asset(quantity.amount, get_token_symbol()), memo)
                ).send();

                // Then transfer the swapped EOS to the target account
                action(
                    permission_level{from, "active"_n},
                    "eosio.token"_n,
                    "transfer"_n,
                    std::make_tuple(from, to, asset(quantity.amount, EOS), memo)
                ).send();
            } else {
                check(false, "Invalid symbol");
            }
        }



        // ----------------------------------------------------
        // HELPERS --------------------------------------------
        // ----------------------------------------------------

        // Gets the token symbol that was selected during initialization,
        // or fails if the contract is not initialized.
        symbol get_token_symbol(){
            config_table _config(get_self(), get_self().value);
            check(_config.exists(), "Contract is not initialized");
            config cfg = _config.get();
            return cfg.token_symbol;
        }

        void enforce_symbol(const asset& quantity){
            check(quantity.symbol == get_token_symbol(), "Wrong token used");
        }

        // Send an amount of EOS from this contract to the user, should
        // only happen after sub_balance has been called to reduce their XYZ balance
        void credit_eos_to(const name& account, const asset& quantity){
            check(quantity.amount > 0, "Credit amount must be greater than 0");

            asset swap_amount = asset(quantity.amount, EOS);
            action(
                permission_level{get_self(), "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(get_self(), account, swap_amount, std::string(""))
            ).send();
        }

        // Allows users to use XYZ tokens to perform actions on the system contract
        // by swapping them for EOS tokens before forwarding the action
        void swap_before_forwarding(const name& account, const asset& quantity){
            check(quantity.symbol == get_token_symbol(), "Wrong token used");
            check(quantity.amount > 0, "Swap before amount must be greater than 0");

            sub_balance(account, quantity);
            add_balance(get_self(), quantity, get_self());
            credit_eos_to(account, quantity);
        }

        // Allows users to get back XYZ tokens from actions that give them EOS tokens
        // by swapping them for XYZ as the last inline action
        void swap_after_forwarding(const name& account, const asset& quantity){
            asset swap_amount = asset(quantity.amount, EOS);
            check(swap_amount.amount > 0, "Swap after amount must be greater than 0");

            action(
                permission_level{account, "active"_n},
                "eosio.token"_n,
                "transfer"_n,
                std::make_tuple(account, get_self(), swap_amount, std::string(""))
            ).send();
        }

        // Gets a given account's balance of EOS
        asset get_eos_balance(const name& account){
            accounts acnts( "eosio.token"_n, account.value );
            const auto& found = acnts.find( EOS.code().raw() );
            if(found == acnts.end()){
                return asset(0, EOS);
            }

            return found->balance;
        }

        // Makes sure that an EOS balance is what it should be after an action.
        // This is to prevent unexpected inline changes to their balances during the
        // forwarding of actions to the system contracts.
        // In cases where the user has notification handlers on their account, they should
        // swap tokens manually first, and then use the `eosio` contract actions directly instead
        // of using the user experience forwarding actions in this contract.
        ACTION enforcebal( const name& account, const asset& expected_eos_balance ){
            asset eos_balance = get_eos_balance(account);
            check(eos_balance == expected_eos_balance, "EOS balance mismatch: " + eos_balance.to_string() + " != " + expected_eos_balance.to_string());
        }

        // Swaps any excess EOS back to XYZ after an action
        ACTION swapexcess( const name& account, const asset& eos_before ){
            require_auth(get_self());
            asset eos_after = get_eos_balance(account);
            if(eos_after > eos_before){
                asset excess = eos_after - eos_before;
                swap_after_forwarding(account, excess);
            }
        }



        // ----------------------------------------------------
        // SYSTEM ACTIONS -------------------------------------
        // ----------------------------------------------------
        // The following actions are all inline actions to the system contract
        // that are forwarded from this contract. They are all wrapped in a swap
        // before or after the action.
        // For details about what each action does, please see the base system contracts.

        ACTION bidname( const name& bidder, const name& newname, const asset& bid ){
            swap_before_forwarding(bidder, bid);

            action(
                permission_level{bidder, "active"_n},
                "eosio"_n,
                "bidname"_n,
                std::make_tuple(bidder, newname, asset(bid.amount, EOS))
            ).send();
        }

        ACTION bidrefund( const name& bidder, const name& newname ){
            auto eos_balance = get_eos_balance(bidder);

            action(
                permission_level{bidder, "active"_n},
                "eosio"_n,
                "bidrefund"_n,
                std::make_tuple(bidder, newname)
            ).send();

            action(
                permission_level{get_self(), "active"_n},
                get_self(),
                "swapexcess"_n,
                std::make_tuple(bidder, eos_balance)
            ).send();
        }

        ACTION buyram( const name& payer, const name& receiver, const asset& quant ){
            swap_before_forwarding(payer, quant);
            action(
                permission_level{payer, "active"_n},
                "eosio"_n,
                "buyram"_n,
                std::make_tuple(payer, receiver, asset(quant.amount, EOS))
            ).send();
        }

        ACTION buyramburn( const name& payer, const asset& quantity, const std::string& memo ){
            swap_before_forwarding(payer, quantity);
            action(
                permission_level{payer, "active"_n},
                "eosio"_n,
                "buyramburn"_n,
                std::make_tuple(payer, asset(quantity.amount, EOS), memo)
            ).send();
        }

        ACTION buyrambytes( name payer, name receiver, uint32_t bytes ){
            rammarket _rammarket("eosio"_n, "eosio"_n.value);
            auto itr = _rammarket.find(RAMCORE.raw());
            const int64_t ram_reserve   = itr->base.balance.amount;
            const int64_t eos_reserve   = itr->quote.balance.amount;
            const int64_t cost          = get_bancor_input( ram_reserve, eos_reserve, bytes );
            const int64_t cost_plus_fee = cost / double(0.995);

            swap_before_forwarding(payer, asset(cost_plus_fee, get_token_symbol()));
            // The balance will be the current + the swapped balance, it just hasn't inlined yet.
            asset current_eos_balance = get_eos_balance(payer) + asset(cost_plus_fee, EOS);

            action(
                permission_level{payer, "active"_n},
                "eosio"_n,
                "buyrambytes"_n,
                std::make_tuple(payer, receiver, bytes)
            ).send();

            // Removes the possibility of the user having more or less EOS than they should
            // due to on_notify of the transfer or buyram.
            action(
                permission_level{payer, "active"_n},
                get_self(),
                "enforcebal"_n,
                std::make_tuple(
                    payer,
                    current_eos_balance - asset(cost_plus_fee, EOS)
                )
            ).send();
        }

        ACTION buyramself( const name& payer, const asset& quant ){
            swap_before_forwarding(payer, quant);
            action(
                permission_level{payer, "active"_n},
                "eosio"_n,
                "buyramself"_n,
                std::make_tuple(payer, asset(quant.amount, EOS))
            ).send();
        }

        ACTION ramburn( const name& owner, const int64_t& bytes, const std::string& memo ){
            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "ramburn"_n,
                std::make_tuple(owner, bytes, memo)
            ).send();
        }

        ACTION ramtransfer( const name& from, const name& to, const int64_t& bytes, const std::string& memo ){
            action(
                permission_level{from, "active"_n},
                "eosio"_n,
                "ramtransfer"_n,
                std::make_tuple(from, to, bytes, memo)
            ).send();
        }

        ACTION sellram( const name& account, const int64_t& bytes ){
            asset eos_before = get_eos_balance(account);

            action(
                permission_level{account, "active"_n},
                "eosio"_n,
                "sellram"_n,
                std::make_tuple(account, bytes)
            ).send();

            action(
                permission_level{get_self(), "active"_n},
                get_self(),
                "swapexcess"_n,
                std::make_tuple(account, eos_before)
            ).send();
        }

        ACTION deposit( const name& owner, const asset& amount ){
            swap_before_forwarding(owner, amount);
            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "deposit"_n,
                std::make_tuple(owner, asset(amount.amount, EOS))
            ).send();
        }

        ACTION buyrex( const name& from, const asset& amount ){
            enforce_symbol(amount);
            // Do not need a swap here because the EOS is already deposited.
            action(
                permission_level{from, "active"_n},
                "eosio"_n,
                "buyrex"_n,
                std::make_tuple(from, asset(amount.amount, EOS))
            ).send();
        }

        ACTION mvfrsavings( const name& owner, const asset& rex ){
            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "mvfrsavings"_n,
                std::make_tuple(owner, rex)
            ).send();
        }

        ACTION mvtosavings( const name& owner, const asset& rex ){
            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "mvtosavings"_n,
                std::make_tuple(owner, rex)
            ).send();
        }

        ACTION sellrex( const name& from, const asset& rex ){
            action(
                permission_level{from, "active"_n},
                "eosio"_n,
                "sellrex"_n,
                std::make_tuple(from, rex)
            ).send();
        }

        ACTION withdraw( const name& owner, const asset& amount ){
            enforce_symbol(amount);

            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "withdraw"_n,
                std::make_tuple(owner, asset(amount.amount, EOS))
            ).send();

            swap_after_forwarding(owner, asset(amount.amount, EOS));
        }

        ACTION newaccount( const name& creator, const name& account_name, const authority& owner, const authority& active ){
            action(
                permission_level{creator, "active"_n},
                "eosio"_n,
                "newaccount"_n,
                std::make_tuple(creator, account_name, owner, active)
            ).send();
        }

        // Simplified account creation action that only requires a public key instead of 2 authority objects
        ACTION newaccount2( const name& creator, const name& account_name, eosio::public_key key ){
            authority auth = authority{
                .threshold = 1,
                .keys = {
                    {.key = key, .weight = 1}
                }
            };

            action(
                permission_level{creator, "active"_n},
                "eosio"_n,
                "newaccount"_n,
                std::make_tuple(creator, account_name, auth, auth)
            ).send();
        }

        ACTION powerup( const name& payer, const name& receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac, const asset& max_payment ){
            // we need to swap back any overages after the powerup, so we need to know how much was in the account before
            // otherwise this contract would have to replicate a large portion of the powerup code which is unnecessary
            asset eos_balance_before_swap = get_eos_balance(payer);

            swap_before_forwarding(payer, max_payment);
            asset eos_payment = asset(max_payment.amount, EOS);
            action(
                permission_level{payer, "active"_n},
                "eosio"_n,
                "powerup"_n,
                std::make_tuple(payer, receiver, days, net_frac, cpu_frac, eos_payment)
            ).send();

            // swap excess back to XYZ
            action(
                permission_level{get_self(), "active"_n},
                get_self(),
                "swapexcess"_n,
                std::make_tuple(payer, eos_balance_before_swap)
            ).send();

        }

        ACTION delegatebw( const name& from, const name& receiver, const asset& stake_net_quantity, const asset& stake_cpu_quantity, const bool& transfer ){
            swap_before_forwarding(from, stake_net_quantity + stake_cpu_quantity);

            action(
                permission_level{from, "active"_n},
                "eosio"_n,
                "delegatebw"_n,
                std::make_tuple(
                    from,
                    receiver,
                    asset(stake_net_quantity.amount, EOS),
                    asset(stake_cpu_quantity.amount, EOS),
                    transfer
                )
            ).send();
        }

        ACTION undelegatebw( const name& from, const name& receiver, const asset& unstake_net_quantity, const asset& unstake_cpu_quantity ){
            enforce_symbol(unstake_cpu_quantity);
            enforce_symbol(unstake_net_quantity);

            action(
                permission_level{from, "active"_n},
                "eosio"_n,
                "undelegatebw"_n,
                std::make_tuple(
                    from,
                    receiver,
                    asset(unstake_net_quantity.amount, EOS),
                    asset(unstake_cpu_quantity.amount, EOS)
                )
            ).send();
        }

        ACTION voteproducer( const name& voter, const name& proxy, const std::vector<name>& producers ){
            action(
                permission_level{voter, "active"_n},
                "eosio"_n,
                "voteproducer"_n,
                std::make_tuple(voter, proxy, producers)
            ).send();
        }

        ACTION voteupdate( const name& voter_name ){
            action(
                permission_level{voter_name, "active"_n},
                "eosio"_n,
                "voteupdate"_n,
                std::make_tuple(voter_name)
            ).send();
        }

        ACTION unstaketorex( const name& owner, const name& receiver, const asset& from_net, const asset& from_cpu ){
            enforce_symbol(from_net);
            enforce_symbol(from_cpu);

            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "unstaketorex"_n,
                std::make_tuple(
                    owner,
                    receiver,
                    asset(from_net.amount, EOS),
                    asset(from_cpu.amount, EOS)
                )
            ).send();
        }

        ACTION refund( const name& owner ){
            auto eos_balance = get_eos_balance(owner);
            action(
                permission_level{owner, "active"_n},
                "eosio"_n,
                "refund"_n,
                std::make_tuple(owner)
            ).send();

            action(
                permission_level{get_self(), "active"_n},
                get_self(),
                "swapexcess"_n,
                std::make_tuple(owner, eos_balance)
            ).send();
        }

        ACTION noop(std::string memo){}
};
