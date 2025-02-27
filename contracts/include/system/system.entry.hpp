#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <eosio/binary_extension.hpp>

namespace system_origin {
struct authority;
};

class [[eosio::contract("system")]] system_contract : public eosio::contract {
public:
   using contract::contract;
   using name   = eosio::name;
   using asset  = eosio::asset;
   using symbol = eosio::symbol;

   struct [[eosio::table]] config {
      symbol token_symbol;
   };

   typedef eosio::singleton<"config"_n, config> config_table;

   // allow account owners to disallow the `swapto` action with their account as destination.
   // This has been requested by exchanges who prefer to receive funds into their hot wallets
   // exclusively via the root `transfer` action.
   struct [[eosio::table]] blocked_recipient {
      name account;

      uint64_t primary_key() const { return account.value; }
   };

   typedef eosio::multi_index<"blocked"_n, blocked_recipient> blocked_table;

   /**
    * Initialize the token with a maximum supply and given token ticker and store a ref to which ticker is selected.
    * This also issues the maximum supply to the system contract itself so that it can use it for
    * swaps.
    * @param maximum_supply - The maximum supply of the token and the symbol of the new token.
    */
   [[eosio::action]] void init(asset maximum_supply);

   // ----------------------------------------------------
   // SYSTEM TOKEN ---------------------------------------
   // ----------------------------------------------------
   [[eosio::action]] void transfer(const name& from, const name& to, const asset& quantity, const std::string& memo);
   [[eosio::action]] void open(const name& owner, const symbol& symbol, const name& ram_payer);
   [[eosio::action]] void close(const name& owner, const symbol& symbol);
   [[eosio::action]] void retire(const name& owner, const asset& quantity, const std::string& memo);

   // ----------------------------------------------------
   // SWAP -----------------------------------------------
   // ----------------------------------------------------
   // When this contract receives EOS tokens, it will swap them for XYZ tokens and credit them to the sender.
   [[eosio::on_notify("eosio.token::transfer")]]
   void on_transfer(const name& from, const name& to, const asset& quantity, const std::string& memo);

   // This action allows exchanges to support "swap & withdraw" for their users and have the swapped tokens flow
   // to the users instead of to their own hot wallets.
   [[eosio::action]] void swapto(const name& from, const name& to, const asset& quantity, const std::string& memo);
   [[eosio::action]] void blockswapto(const name& account, const bool block);
   [[eosio::action]] void enforcebal(const name& account, const asset& expected_eos_balance);
   [[eosio::action]] void swapexcess(const name& account, const asset& eos_before);
   [[eosio::action]] void swaptrace(const name& account, const asset& quantity);

   // ----------------------------------------------------
   // SYSTEM ACTIONS -------------------------------------
   // ----------------------------------------------------
   // The following actions are all inline actions to the system contract
   // that are forwarded from this contract. They are all wrapped in a swap
   // before or after the action.
   // For details about what each action does, please see the base system contracts.

   [[eosio::action]] void bidname(const name& bidder, const name& newname, const asset& bid);
   [[eosio::action]] void bidrefund(const name& bidder, const name& newname);
   [[eosio::action]] void buyram(const name& payer, const name& receiver, const asset& quant);
   [[eosio::action]] void buyramburn(const name& payer, const asset& quantity, const std::string& memo);
   [[eosio::action]] void buyrambytes(name payer, name receiver, uint32_t bytes);
   [[eosio::action]] void buyramself(const name& payer, const asset& quant);
   [[eosio::action]] void ramburn(const name& owner, const int64_t& bytes, const std::string& memo);
   [[eosio::action]] void ramtransfer(const name& from, const name& to, const int64_t& bytes, const std::string& memo);
   [[eosio::action]] void sellram(const name& account, const int64_t& bytes);
   [[eosio::action]] void deposit(const name& owner, const asset& amount);
   [[eosio::action]] void buyrex(const name& from, const asset& amount);
   [[eosio::action]] void mvfrsavings(const name& owner, const asset& rex);
   [[eosio::action]] void mvtosavings(const name& owner, const asset& rex);
   [[eosio::action]] void sellrex(const name& from, const asset& rex);
   [[eosio::action]] void withdraw(const name& owner, const asset& amount);
   [[eosio::action]] void newaccount(const name& creator, const name& account_name,
                                     const system_origin::authority& owner, const system_origin::authority& active);
   [[eosio::action]] void newaccount2(const name& creator, const name& account_name, eosio::public_key key);
   [[eosio::action]] void powerup(const name& payer, const name& receiver, uint32_t days, int64_t net_frac,
                                  int64_t cpu_frac, const asset& max_payment);
   [[eosio::action]] void delegatebw(const name& from, const name& receiver, const asset& stake_net_quantity,
                                     const asset& stake_cpu_quantity, const bool& transfer);
   [[eosio::action]] void undelegatebw(const name& from, const name& receiver, const asset& unstake_net_quantity,
                                       const asset& unstake_cpu_quantity);
   [[eosio::action]] void voteproducer(const name& voter, const name& proxy, const std::vector<name>& producers);
   [[eosio::action]] void voteupdate(const name& voter_name);
   [[eosio::action]] void unstaketorex(const name& owner, const name& receiver, const asset& from_net,
                                       const asset& from_cpu);
   [[eosio::action]] void refund(const name& owner);
   [[eosio::action]] void claimrewards(const name owner);
   [[eosio::action]] void linkauth(name account, name code, name type, name requirement,
                                   eosio::binary_extension<name> authorized_by);
   [[eosio::action]] void unlinkauth(name account, name code, name type, eosio::binary_extension<name> authorized_by);
   [[eosio::action]] void updateauth(name account, name permission, name parent, system_origin::authority auth,
                                     eosio::binary_extension<name> authorized_by);
   [[eosio::action]] void deleteauth(name account, name permission, eosio::binary_extension<name> authorized_by);
   [[eosio::action]] void setabi(const name& account, const std::vector<char>& abi,
                                 const eosio::binary_extension<std::string>& memo);
   [[eosio::action]] void setcode(const name& account, uint8_t vmtype, uint8_t vmversion, const std::vector<char>& code,
                                  const eosio::binary_extension<std::string>& memo);
   [[eosio::action]] void donatetorex(const name& payer, const asset& quantity, const std::string& memo);
   [[eosio::action]] void giftram(const name& from, const name& receiver, const int64_t& ram_bytes,
                                  const std::string& memo);
   [[eosio::action]] void ungiftram(const name& from, const name& to, const std::string& memo);
   [[eosio::action]] void noop(std::string memo);


   // ----------------------------------------------------
   // [[eosio::action]] void WRAPPERS ------------------------------------
   // ----------------------------------------------------
   using init_action         = eosio::action_wrapper<"init"_n, &system_contract::init>;
   using transfer_action     = eosio::action_wrapper<"transfer"_n, &system_contract::transfer>;
   using open_action         = eosio::action_wrapper<"open"_n, &system_contract::open>;
   using close_action        = eosio::action_wrapper<"close"_n, &system_contract::close>;
   using retire_action       = eosio::action_wrapper<"retire"_n, &system_contract::retire>;
   using swapto_action       = eosio::action_wrapper<"swapto"_n, &system_contract::swapto>;
   using blockswapto_action  = eosio::action_wrapper<"blockswapto"_n, &system_contract::blockswapto>;
   using enforcebal_action   = eosio::action_wrapper<"enforcebal"_n, &system_contract::enforcebal>;
   using swapexcess_action   = eosio::action_wrapper<"swapexcess"_n, &system_contract::swapexcess>;
   using swaptrace_action    = eosio::action_wrapper<"swaptrace"_n, &system_contract::swaptrace>;
   using bidname_action      = eosio::action_wrapper<"bidname"_n, &system_contract::bidname>;
   using bidrefund_action    = eosio::action_wrapper<"bidrefund"_n, &system_contract::bidrefund>;
   using buyram_action       = eosio::action_wrapper<"buyram"_n, &system_contract::buyram>;
   using buyramburn_action   = eosio::action_wrapper<"buyramburn"_n, &system_contract::buyramburn>;
   using buyrambytes_action  = eosio::action_wrapper<"buyrambytes"_n, &system_contract::buyrambytes>;
   using buyramself_action   = eosio::action_wrapper<"buyramself"_n, &system_contract::buyramself>;
   using ramburn_action      = eosio::action_wrapper<"ramburn"_n, &system_contract::ramburn>;
   using ramtransfer_action  = eosio::action_wrapper<"ramtransfer"_n, &system_contract::ramtransfer>;
   using sellram_action      = eosio::action_wrapper<"sellram"_n, &system_contract::sellram>;
   using deposit_action      = eosio::action_wrapper<"deposit"_n, &system_contract::deposit>;
   using buyrex_action       = eosio::action_wrapper<"buyrex"_n, &system_contract::buyrex>;
   using mvfrsavings_action  = eosio::action_wrapper<"mvfrsavings"_n, &system_contract::mvfrsavings>;
   using mvtosavings_action  = eosio::action_wrapper<"mvtosavings"_n, &system_contract::mvtosavings>;
   using sellrex_action      = eosio::action_wrapper<"sellrex"_n, &system_contract::sellrex>;
   using withdraw_action     = eosio::action_wrapper<"withdraw"_n, &system_contract::withdraw>;
   using newaccount_action   = eosio::action_wrapper<"newaccount"_n, &system_contract::newaccount>;
   using newaccount2_action  = eosio::action_wrapper<"newaccount2"_n, &system_contract::newaccount2>;
   using powerup_action      = eosio::action_wrapper<"powerup"_n, &system_contract::powerup>;
   using delegatebw_action   = eosio::action_wrapper<"delegatebw"_n, &system_contract::delegatebw>;
   using undelegatebw_action = eosio::action_wrapper<"undelegatebw"_n, &system_contract::undelegatebw>;
   using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &system_contract::voteproducer>;
   using voteupdate_action   = eosio::action_wrapper<"voteupdate"_n, &system_contract::voteupdate>;
   using unstaketorex_action = eosio::action_wrapper<"unstaketorex"_n, &system_contract::unstaketorex>;
   using refund_action       = eosio::action_wrapper<"refund"_n, &system_contract::refund>;
   using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &system_contract::claimrewards>;
   using linkauth_action     = eosio::action_wrapper<"linkauth"_n, &system_contract::linkauth>;
   using unlinkauth_action   = eosio::action_wrapper<"unlinkauth"_n, &system_contract::unlinkauth>;
   using updateauth_action   = eosio::action_wrapper<"updateauth"_n, &system_contract::updateauth>;
   using deleteauth_action   = eosio::action_wrapper<"deleteauth"_n, &system_contract::deleteauth>;
   using setabi_action       = eosio::action_wrapper<"setabi"_n, &system_contract::setabi>;
   using setcode_action      = eosio::action_wrapper<"setcode"_n, &system_contract::setcode>;
   using donatetorex_action  = eosio::action_wrapper<"donatetorex"_n, &system_contract::donatetorex>;
   using giftram_action      = eosio::action_wrapper<"giftram"_n, &system_contract::giftram>;
   using ungiftram_action    = eosio::action_wrapper<"ungiftram"_n, &system_contract::ungiftram>;
   using noop_action         = eosio::action_wrapper<"noop"_n, &system_contract::noop>;

private:
   void   add_balance(const name& owner, const asset& value, const name& ram_payer);
   void   sub_balance(const name& owner, const asset& value);
   symbol get_token_symbol();
   void   enforce_symbol(const asset& quantity);
   void   credit_eos_to(const name& account, const asset& quantity);
   void   swap_before_forwarding(const name& account, const asset& quantity);
   void   swap_after_forwarding(const name& account, const asset& quantity);
   asset  get_eos_balance(const name& account);
};
