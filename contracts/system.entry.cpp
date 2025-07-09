//contractName:system
#include <system/system.entry.hpp>

#include <system/token.hpp>
#include <system/oldsystem.hpp>

using namespace eosio;
using namespace system_origin;

/**
 * Initialize the token with a maximum supply and given token ticker and store a ref to which ticker is selected.
 * This also issues the maximum supply to the system contract itself so that it can use it for
 * swaps.
 * @param maximum_supply - The maximum supply of the token and the symbol of the new token.
 */
void system_contract::init(asset maximum_supply) {
   require_auth(get_self());
   config_table _config(get_self(), get_self().value);
   check(!_config.exists(), "This system contract is already initialized");

   auto sym = maximum_supply.symbol;
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   _config.set(config{.token_symbol = sym}, get_self());

   stats statstable(get_self(), sym.code().raw());
   statstable.emplace(get_self(), [&](auto& s) {
      s.supply     = maximum_supply;
      s.max_supply = maximum_supply;
      s.issuer     = get_self();
   });

   add_balance(get_self(), maximum_supply, get_self());
}


// ----------------------------------------------------
// SYSTEM TOKEN ---------------------------------------
// ----------------------------------------------------

void system_contract::transfer(const name& from, const name& to, const asset& quantity, const std::string& memo) {
   check(from != to, "cannot transfer to self");
   require_auth(from);
   check(is_account(to), "to account does not exist");

   auto        sym = quantity.symbol.code();
   stats       statstable(get_self(), sym.raw());
   const auto& st = statstable.get(sym.raw());

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto payer = has_auth(to) ? to : from;

   sub_balance(from, quantity);
   add_balance(to, quantity, payer);

   require_recipient(from);
   require_recipient(to);

   // If `from` is sending XYZ tokens to this contract
   // they are swapping from XYZ to EOS
   if (to == get_self()) {
      check(quantity.symbol == get_token_symbol(), "Wrong token used");
      credit_eos_to(from, quantity);
   }
}

void system_contract::open(const name& owner, const symbol& symbol, const name& ram_payer) {
   require_auth(ram_payer);

   check(is_account(owner), "owner account does not exist");

   auto        sym_code_raw = symbol.code().raw();
   stats       statstable(get_self(), sym_code_raw);
   const auto& st = statstable.get(sym_code_raw, "symbol does not exist");
   check(st.supply.symbol == symbol, "symbol precision mismatch");

   accounts acnts(get_self(), owner.value);
   auto     it = acnts.find(sym_code_raw);
   if (it == acnts.end()) {
      acnts.emplace(ram_payer, [&](auto& a) {
         a.balance = asset{0, symbol};
         a.released = ram_payer == owner;
      });
   }
}

void system_contract::close(const name& owner, const symbol& symbol) {
   require_auth(owner);
   accounts acnts(get_self(), owner.value);
   auto     it = acnts.find(symbol.code().raw());
   check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
   check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
   acnts.erase(it);
}

void system_contract::add_balance(const name& owner, const asset& value, const name& ram_payer) {
   accounts to_acnts(get_self(), owner.value);
   auto     to = to_acnts.find(value.symbol.code().raw());
   if (to == to_acnts.end()) {
      to_acnts.emplace(ram_payer == owner ? owner : get_self(), [&](auto& a) {
         a.balance = value;
         a.released = ram_payer == owner;
      });
   } else {
      to_acnts.modify(to, same_payer, [&](auto& a) { a.balance += value; });
   }
}

void system_contract::sub_balance(const name& owner, const asset& value) {
   accounts from_acnts(get_self(), owner.value);

   const auto& from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
   check(from.balance.amount >= value.amount, "overdrawn balance");

   if(!from.released){
      auto balance = from.balance;
      // This clears out the RAM consumed by the scope overhead.
      from_acnts.erase( from );
      from_acnts.emplace( owner, [&]( auto& a ){
         a.balance = balance - value;
         a.released = true;
      });
   } else {
      from_acnts.modify( from, owner, [&]( auto& a ) {
         a.balance -= value;
      });
   }
}

// ----------------------------------------------------
// SWAP -----------------------------------------------
// ----------------------------------------------------

// When this contract receives EOS tokens, it will swap them for XYZ tokens and credit them to the sender.

void system_contract::on_transfer(const name& from, const name& to, const asset& quantity, const std::string& memo) {
   if (from == get_self() || to != get_self())
      return;
   check(quantity.amount > 0, "Swap amount must be greater than 0");

   // Ignore for system accounts, otherwise when unstaking or selling ram this will swap EOS for
   // XYZ and credit them to the sending account which will lock those tokens.
   if (from == "eosio.ram"_n)
      return;
   if (from == "eosio.stake"_n)
      return;

   check(quantity.symbol == EOS, "Invalid symbol");
   asset swap_amount = asset(quantity.amount, get_token_symbol());
   transfer_action(get_self(), {{get_self(), "active"_n}}).send(get_self(), from, swap_amount, std::cref(memo));
}

// Allows an account to block themselves from being a recipient of the `swapto` action.
void system_contract::blockswapto(const name& account, const bool block) {
   // The account owner or this contract can block or unblock an account.
   if (!has_auth(get_self())) {
      require_auth(account);
   }

   blocked_table _blocked(get_self(), get_self().value);
   auto          itr = _blocked.find(account.value);
   if (block) {
      if (itr == _blocked.end()) {
         _blocked.emplace(account, [&](auto& b) { b.account = account; });
      }
   } else {
      if (itr != _blocked.end()) {
         _blocked.erase(itr);
      }
   }
}


// This action allows exchanges to support "swap & withdraw" for their users and have the swapped tokens flow
// to the users instead of to their own hot wallets.
void system_contract::swapto(const name& from, const name& to, const asset& quantity, const std::string& memo) {
   require_auth(from);

   blocked_table _blocked(get_self(), get_self().value);
   auto          itr = _blocked.find(to.value);
   check(itr == _blocked.end(), "Recipient is blocked from receiving swapped tokens: " + to.to_string());

   if (quantity.symbol == EOS) {
      // First swap the EOS to XYZ and credit it to the user
      transfer_action("eosio.token"_n, {{from, "active"_n}}).send(from, get_self(), quantity, std::cref(memo));

      // Then transfer the swapped XYZ to the target account
      transfer_action(get_self(), {{from, "active"_n}}).send(from, to, asset(quantity.amount, get_token_symbol()), std::cref(memo));
   } else if (quantity.symbol == get_token_symbol()) {
      // First swap the XYZ to EOS and credit it to the user
      transfer_action(get_self(), {{from, "active"_n}}).send(from, get_self(), quantity, std::cref(memo));

      // Then transfer the swapped EOS to the target account
      transfer_action("eosio.token"_n, {{from, "active"_n}}).send(from, to, asset(quantity.amount, EOS), std::cref(memo));
   } else {
      check(false, "Invalid symbol");
   }
}



// ----------------------------------------------------
// HELPERS --------------------------------------------
// ----------------------------------------------------

// Gets the token symbol that was selected during initialization,
// or fails if the contract is not initialized.
symbol system_contract::get_token_symbol() {
   config_table _config(get_self(), get_self().value);
   check(_config.exists(), "Contract is not initialized");
   config cfg = _config.get();
   return cfg.token_symbol;
}

// Enforces that the given asset has the right token symbol (XYZ)
void system_contract::enforce_symbol(const asset& quantity) {
   check(quantity.symbol == get_token_symbol(), "Wrong token used");
}

// Send an amount of EOS from this contract to the user, should
// only happen after sub_balance has been called to reduce their XYZ balance
void system_contract::credit_eos_to(const name& account, const asset& quantity) {
   check(quantity.amount > 0, "Credit amount must be greater than 0");

   asset swap_amount = asset(quantity.amount, EOS);
   transfer_action("eosio.token"_n, {{get_self(), "active"_n}}).send(get_self(), account, swap_amount, std::string(""));
}

// Allows users to use XYZ tokens to perform actions on the system contract
// by swapping them for EOS tokens before forwarding the action
void system_contract::swap_before_forwarding(const name& account, const asset& quantity) {
   check(quantity.symbol == get_token_symbol(), "Wrong token used");
   check(quantity.amount > 0, "Swap before amount must be greater than 0");

   swaptrace_action(get_self(), {{get_self(), "active"_n}}).send(account, quantity);
   sub_balance(account, quantity);
   add_balance(get_self(), quantity, get_self());
   credit_eos_to(account, quantity);
}

// Allows users to get back XYZ tokens from actions that give them EOS tokens
// by swapping them for XYZ as the last inline action
void system_contract::swap_after_forwarding(const name& account, const asset& quantity) {
   asset swap_amount = asset(quantity.amount, EOS);
   check(swap_amount.amount > 0, "Swap after amount must be greater than 0");

   transfer_action("eosio.token"_n, {{account, "active"_n}}).send(account, get_self(), swap_amount, std::string(""));
}

// Gets a given account's balance of EOS
asset system_contract::get_eos_balance(const name& account) {
   eosio_token::accounts acnts("eosio.token"_n, account.value);
   const auto& found = acnts.find(EOS.code().raw());
   if (found == acnts.end()) {
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
void system_contract::enforcebal(const name& account, const asset& expected_eos_balance) {
   asset eos_balance = get_eos_balance(account);
   check(eos_balance == expected_eos_balance,
         "EOS balance mismatch: " + eos_balance.to_string() + " != " + expected_eos_balance.to_string());
}

// Swaps any excess EOS back to XYZ after an action
void system_contract::swapexcess(const name& account, const asset& eos_before) {
   require_auth(get_self());
   asset eos_after = get_eos_balance(account);
   if (eos_after > eos_before) {
      asset excess = eos_after - eos_before;
      swap_after_forwarding(account, excess);
   }
}

void system_contract::swaptrace(const name& account, const asset& quantity) {
   require_auth(get_self());
}

// ----------------------------------------------------
// SYSTEM ACTIONS -------------------------------------
// ----------------------------------------------------
// The following actions are all inline actions to the system contract
// that are forwarded from this contract. They are all wrapped in a swap
// before or after the action.
// For details about what each action does, please see the base system contracts.

void system_contract::bidname(const name& bidder, const name& newname, const asset& bid) {
   require_auth(bidder);
   swap_before_forwarding(bidder, bid);

   bidname_action("eosio"_n, {{bidder, "active"_n}}).send(bidder, newname, asset(bid.amount, EOS));
}

void system_contract::bidrefund(const name& bidder, const name& newname) {
   require_auth(bidder);
   auto eos_balance = get_eos_balance(bidder);

   bidrefund_action("eosio"_n, {{bidder, "active"_n}}).send(bidder, newname);
   swapexcess_action(get_self(), {{get_self(), "active"_n}}).send(bidder, eos_balance);
}

void system_contract::buyram(const name& payer, const name& receiver, const asset& quant) {
   require_auth(payer);
   swap_before_forwarding(payer, quant);
   buyram_action("eosio"_n, {{payer, "active"_n}}).send(payer, receiver, asset(quant.amount, EOS));
}

void system_contract::buyramburn(const name& payer, const asset& quantity, const std::string& memo) {
   require_auth(payer);
   swap_before_forwarding(payer, quantity);
   buyramburn_action("eosio"_n, {{payer, "active"_n}}).send(payer, asset(quantity.amount, EOS), std::cref(memo));
}

void system_contract::buyrambytes(name payer, name receiver, uint32_t bytes) {
   require_auth(payer);
   rammarket     _rammarket("eosio"_n, "eosio"_n.value);
   auto          itr           = _rammarket.find(RAMCORE.raw());
   const int64_t ram_reserve   = itr->base.balance.amount;
   const int64_t eos_reserve   = itr->quote.balance.amount;
   const int64_t cost          = get_bancor_input(ram_reserve, eos_reserve, bytes);
   const int64_t cost_plus_fee = cost / double(0.995);

   swap_before_forwarding(payer, asset(cost_plus_fee, get_token_symbol()));

   buyrambytes_action("eosio"_n, {{payer, "active"_n}}).send(payer, receiver, bytes);

   // The user's balance should not have changed from the amount it was before
   // any swaps took place.
   enforcebal_action( get_self(), {{payer, "active"_n}}).send(payer, get_eos_balance(payer));
}

void system_contract::buyramself(const name& payer, const asset& quant) {
   require_auth(payer);
   swap_before_forwarding(payer, quant);
   buyramself_action("eosio"_n, {{payer, "active"_n}}).send(payer, asset(quant.amount, EOS));
}

void system_contract::ramburn(const name& owner, const int64_t& bytes, const std::string& memo) {
   require_auth(owner);
   ramburn_action("eosio"_n, {{owner, "active"_n}}).send(owner, bytes, std::cref(memo));
}

void system_contract::ramtransfer(const name& from, const name& to, const int64_t& bytes, const std::string& memo) {
   require_auth(from);
   ramtransfer_action("eosio"_n, {{from, "active"_n}}).send(from, to, bytes, std::cref(memo));
}

void system_contract::sellram(const name& account, const int64_t& bytes) {
   require_auth(account);
   asset eos_before = get_eos_balance(account);

   sellram_action("eosio"_n, {{account, "active"_n}}).send(account, bytes);
   swapexcess_action(get_self(), {{get_self(), "active"_n}}).send(account, eos_before);
}

void system_contract::deposit(const name& owner, const asset& amount) {
   require_auth(owner);
   swap_before_forwarding(owner, amount);
   deposit_action("eosio"_n, {{owner, "active"_n}}).send(owner, asset(amount.amount, EOS));
}

void system_contract::buyrex(const name& from, const asset& amount) {
   require_auth(from);
   enforce_symbol(amount);
   // Do not need a swap here because the EOS is already deposited.
   buyrex_action("eosio"_n, {{from, "active"_n}}).send(from, asset(amount.amount, EOS));
}

void system_contract::mvfrsavings(const name& owner, const asset& rex) {
   require_auth(owner);
   mvfrsavings_action("eosio"_n, {{owner, "active"_n}}).send(owner, rex);
}

void system_contract::mvtosavings(const name& owner, const asset& rex) {
   require_auth(owner);
   mvtosavings_action("eosio"_n, {{owner, "active"_n}}).send(owner, rex);
}

void system_contract::sellrex(const name& from, const asset& rex) {
   require_auth(from);
   sellrex_action("eosio"_n, {{from, "active"_n}}).send(from, rex);
}

void system_contract::withdraw(const name& owner, const asset& amount) {
   require_auth(owner);
   enforce_symbol(amount);

   withdraw_action("eosio"_n, {{owner, "active"_n}}).send(owner, asset(amount.amount, EOS));

   swap_after_forwarding(owner, asset(amount.amount, EOS));
}

void system_contract::newaccount(const name& creator, const name& name, const authority& owner,
                                 const authority& active) {
   require_auth(creator);
   newaccount_action("eosio"_n, {{creator, "active"_n}}).send(creator, name, std::cref(owner), std::cref(active));
}

// Simplified account creation action that only requires a public key instead of 2 authority objects
void system_contract::newaccount2(const name& creator, const name& name, eosio::public_key key) {
   require_auth(creator);
   authority auth{.threshold = 1, .keys = {{.key = key, .weight = 1}}};

   newaccount_action("eosio"_n, {{creator, "active"_n}}).send(creator, name, auth, auth);
}

void system_contract::powerup(const name& payer, const name& receiver, uint32_t days, int64_t net_frac,
                              int64_t cpu_frac, const asset& max_payment) {
   require_auth(payer);
   // we need to swap back any overages after the powerup, so we need to know how much was in the account before
   // otherwise this contract would have to replicate a large portion of the powerup code which is unnecessary
   asset eos_balance_before_swap = get_eos_balance(payer);

   swap_before_forwarding(payer, max_payment);
   asset eos_payment = asset(max_payment.amount, EOS);
   powerup_action("eosio"_n, {{payer, "active"_n}}).send(payer, receiver, days, net_frac, cpu_frac, eos_payment);

   // swap excess back to XYZ
   swapexcess_action(get_self(), {{get_self(), "active"_n}}).send(payer, eos_balance_before_swap);
}

void system_contract::delegatebw(const name& from, const name& receiver, const asset& stake_net_quantity,
                                 const asset& stake_cpu_quantity, const bool& transfer) {
   require_auth(from);
   swap_before_forwarding(from, stake_net_quantity + stake_cpu_quantity);

   delegatebw_action("eosio"_n, {{from, "active"_n}}).
      send(from, receiver, asset(stake_net_quantity.amount, EOS), asset(stake_cpu_quantity.amount, EOS), transfer);
}

void system_contract::undelegatebw(const name& from, const name& receiver, const asset& unstake_net_quantity,
                                   const asset& unstake_cpu_quantity) {
   require_auth(from);
   enforce_symbol(unstake_cpu_quantity);
   enforce_symbol(unstake_net_quantity);

   undelegatebw_action("eosio"_n, {{from, "active"_n}}).
      send(from, receiver, asset(unstake_net_quantity.amount, EOS), asset(unstake_cpu_quantity.amount, EOS));
}

void system_contract::voteproducer(const name& voter, const name& proxy, const std::vector<name>& producers) {
   require_auth(voter);
   voteproducer_action("eosio"_n, {{voter, "active"_n}}).send(voter, proxy, std::cref(producers));
}

void system_contract::voteupdate(const name& voter_name) {
   require_auth(voter_name);
   voteupdate_action("eosio"_n, {{voter_name, "active"_n}}).send(voter_name);
}

void system_contract::unstaketorex(const name& owner, const name& receiver, const asset& from_net,
                                   const asset& from_cpu) {
   require_auth(owner);
   enforce_symbol(from_net);
   enforce_symbol(from_cpu);

   unstaketorex_action("eosio"_n, {{owner, "active"_n}}).
      send(owner, receiver, asset(from_net.amount, EOS), asset(from_cpu.amount, EOS));
}

void system_contract::refund(const name& owner) {
   require_auth(owner);
   auto eos_balance = get_eos_balance(owner);

   refund_action("eosio"_n, {{owner, "active"_n}}).send(owner);

   swapexcess_action(get_self(), {{get_self(), "active"_n}}).send(owner, eos_balance);
}

void system_contract::claimrewards(const name owner) {
   require_auth(owner);
   auto eos_balance = get_eos_balance(owner);

   claimrewards_action("eosio"_n, {{owner, "active"_n}}).send(owner);

   swapexcess_action(get_self(), {{get_self(), "active"_n}}).send(owner, eos_balance);
}

void system_contract::linkauth(name account, name code, name type, name requirement,
                               binary_extension<name> authorized_by) {
   require_auth(account);
   linkauth_action("eosio"_n, {{account, "active"_n}}).send(account, code, type, requirement, authorized_by);
}

void system_contract::unlinkauth(name account, name code, name type, binary_extension<name> authorized_by) {
   require_auth(account);
   unlinkauth_action("eosio"_n, {{account, "active"_n}}).send(account, code, type, authorized_by);
}

void system_contract::updateauth(name account, name permission, name parent, authority auth,
                                 binary_extension<name> authorized_by) {
   require_auth(account);
   updateauth_action("eosio"_n, {{account, "active"_n}}).send(account, permission, parent, std::cref(auth), authorized_by);
}

void system_contract::deleteauth(name account, name permission, binary_extension<name> authorized_by) {
   require_auth(account);
   deleteauth_action("eosio"_n, {{account, "active"_n}}).send(account, permission, authorized_by);
}

void system_contract::setabi(const name& account, const std::vector<char>& abi,
                             const binary_extension<std::string>& memo) {
   require_auth(account);
   setabi_action("eosio"_n, {{account, "active"_n}}).send(account, std::cref(abi), std::cref(memo));
}

void system_contract::setcode(const name& account, uint8_t vmtype, uint8_t vmversion, const std::vector<char>& code,
                              const binary_extension<std::string>& memo) {
   require_auth(account);
   setcode_action("eosio"_n, {{account, "active"_n}}).send(account, vmtype, vmversion, std::cref(code), std::cref(memo));
}

void system_contract::donatetorex(const name& payer, const asset& quantity, const std::string& memo) {
   require_auth(payer);
   swap_before_forwarding(payer, quantity);
   donatetorex_action("eosio"_n, {{payer, "active"_n}}).send(payer, asset(quantity.amount, EOS), std::cref(memo));
}

void system_contract::giftram(const name& from, const name& receiver, const int64_t& ram_bytes,
                              const std::string& memo) {
   require_auth(from);
   giftram_action("eosio"_n, {{from, "active"_n}}).send(from, receiver, ram_bytes, std::cref(memo));
}

void system_contract::ungiftram(const name& from, const name& to, const std::string& memo) {
   require_auth(from);
   ungiftram_action("eosio"_n, {{from, "active"_n}}).send(from, to, std::cref(memo));
}


void system_contract::noop(std::string memo) {}
