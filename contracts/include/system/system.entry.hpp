#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <eosio/binary_extension.hpp>
#include <system/token.hpp>
#include <system/oldsystem.hpp>

namespace system_origin {
struct authority;
};

class [[eosio::contract("system")]] system_contract : public eosio::contract {
public:
   using contract::contract;

   TABLE config { eosio::symbol token_symbol; };

   typedef eosio::singleton<"config"_n, config> config_table;

   // allow account owners to disallow the `swapto` action with their account as destination.
   // This has been requested by exchanges who prefer to receive funds into their hot wallets
   // exclusively via the root `transfer` action.
   TABLE blocked_recipient {
      eosio::name account;

      uint64_t primary_key() const {
         return account.value;
      }
   };

   typedef eosio::multi_index<"blocked"_n, blocked_recipient> blocked_table;

   /**
    * Initialize the token with a maximum supply and given token ticker and store a ref to which ticker is selected.
    * This also issues the maximum supply to the system contract itself so that it can use it for
    * swaps.
    * @param maximum_supply - The maximum supply of the token and the symbol of the new token.
    */
   ACTION init(eosio::asset maximum_supply);

   // ----------------------------------------------------
   // SYSTEM TOKEN ---------------------------------------
   // ----------------------------------------------------
   ACTION transfer(const eosio::name& from, const eosio::name& to, const eosio::asset& quantity,
                   const std::string& memo);
   ACTION open(const eosio::name& owner, const eosio::symbol& symbol, const eosio::name& ram_payer);
   ACTION close(const eosio::name& owner, const eosio::symbol& symbol);
   ACTION retire(const eosio::name& owner, const eosio::asset& quantity, const std::string& memo);

   // ----------------------------------------------------
   // SWAP -----------------------------------------------
   // ----------------------------------------------------
   // When this contract receives EOS tokens, it will swap them for XYZ tokens and credit them to the sender.
   [[eosio::on_notify("eosio.token::transfer")]]
   void on_transfer(const eosio::name& from, const eosio::name& to, const eosio::asset& quantity,
                    const std::string& memo);

   // This action allows exchanges to support "swap & withdraw" for their users and have the swapped tokens flow
   // to the users instead of to their own hot wallets.
   ACTION swapto(const eosio::name& from, const eosio::name& to, const eosio::asset& quantity, const std::string& memo);
   ACTION blockswapto(const eosio::name& account, const bool block);
   ACTION enforcebal(const eosio::name& account, const eosio::asset& expected_eos_balance);
   ACTION swapexcess(const eosio::name& account, const eosio::asset& eos_before);

   // ----------------------------------------------------
   // SYSTEM ACTIONS -------------------------------------
   // ----------------------------------------------------
   // The following actions are all inline actions to the system contract
   // that are forwarded from this contract. They are all wrapped in a swap
   // before or after the action.
   // For details about what each action does, please see the base system contracts.

   ACTION bidname(const eosio::name& bidder, const eosio::name& newname, const eosio::asset& bid);
   ACTION bidrefund(const eosio::name& bidder, const eosio::name& newname);
   ACTION buyram(const eosio::name& payer, const eosio::name& receiver, const eosio::asset& quant);
   ACTION buyramburn(const eosio::name& payer, const eosio::asset& quantity, const std::string& memo);
   ACTION buyrambytes(eosio::name payer, eosio::name receiver, uint32_t bytes);
   ACTION buyramself(const eosio::name& payer, const eosio::asset& quant);
   ACTION ramburn(const eosio::name& owner, const int64_t& bytes, const std::string& memo);
   ACTION ramtransfer(const eosio::name& from, const eosio::name& to, const int64_t& bytes, const std::string& memo);
   ACTION sellram(const eosio::name& account, const int64_t& bytes);
   ACTION deposit(const eosio::name& owner, const eosio::asset& amount);
   ACTION buyrex(const eosio::name& from, const eosio::asset& amount);
   ACTION mvfrsavings(const eosio::name& owner, const eosio::asset& rex);
   ACTION mvtosavings(const eosio::name& owner, const eosio::asset& rex);
   ACTION sellrex(const eosio::name& from, const eosio::asset& rex);
   ACTION withdraw(const eosio::name& owner, const eosio::asset& amount);
   ACTION newaccount(const eosio::name& creator, const eosio::name& account_name, const system_origin::authority& owner,
                     const system_origin::authority& active);
   ACTION newaccount2(const eosio::name& creator, const eosio::name& account_name, eosio::public_key key);
   ACTION powerup(const eosio::name& payer, const eosio::name& receiver, uint32_t days, int64_t net_frac,
                  int64_t cpu_frac, const eosio::asset& max_payment);
   ACTION delegatebw(const eosio::name& from, const eosio::name& receiver, const eosio::asset& stake_net_quantity,
                     const eosio::asset& stake_cpu_quantity, const bool& transfer);
   ACTION undelegatebw(const eosio::name& from, const eosio::name& receiver, const eosio::asset& unstake_net_quantity,
                       const eosio::asset& unstake_cpu_quantity);
   ACTION voteproducer(const eosio::name& voter, const eosio::name& proxy, const std::vector<eosio::name>& producers);
   ACTION voteupdate(const eosio::name& voter_name);
   ACTION unstaketorex(const eosio::name& owner, const eosio::name& receiver, const eosio::asset& from_net,
                       const eosio::asset& from_cpu);
   ACTION refund(const eosio::name& owner);
   ACTION claimrewards(const eosio::name owner);
   ACTION linkauth(eosio::name account, eosio::name code, eosio::name type, eosio::name requirement,
                   eosio::binary_extension<eosio::name> authorized_by);
   ACTION unlinkauth(eosio::name account, eosio::name code, eosio::name type,
                     eosio::binary_extension<eosio::name> authorized_by);
   ACTION updateauth(eosio::name account, eosio::name permission, eosio::name parent, system_origin::authority auth,
                     eosio::binary_extension<eosio::name> authorized_by);
   ACTION deleteauth(eosio::name account, eosio::name permission, eosio::binary_extension<eosio::name> authorized_by);
   ACTION setabi(const eosio::name& account, const std::vector<char>& abi,
                 const eosio::binary_extension<std::string>& memo);
   ACTION setcode(const eosio::name& account, uint8_t vmtype, uint8_t vmversion, const std::vector<char>& code,
                  const eosio::binary_extension<std::string>& memo);
   ACTION donatetorex(const eosio::name& payer, const eosio::asset& quantity, const std::string& memo);
   ACTION giftram(const eosio::name& from, const eosio::name& receiver, const int64_t& ram_bytes,
                  const std::string& memo);
   ACTION ungiftram(const eosio::name& from, const eosio::name& to, const std::string& memo);
   ACTION noop(std::string memo);

private:
   void          add_balance(const eosio::name& owner, const eosio::asset& value, const eosio::name& ram_payer);
   void          sub_balance(const eosio::name& owner, const eosio::asset& value);
   eosio::symbol get_token_symbol();
   void          enforce_symbol(const eosio::asset& quantity);
   void          credit_eos_to(const eosio::name& account, const eosio::asset& quantity);
   void          swap_before_forwarding(const eosio::name& account, const eosio::asset& quantity);
   void          swap_after_forwarding(const eosio::name& account, const eosio::asset& quantity);
   eosio::asset  get_eos_balance(const eosio::name& account);
};
