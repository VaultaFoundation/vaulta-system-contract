#include <boost/test/unit_test.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fc/log/logger.hpp>
#include <eosio/chain/exceptions.hpp>

#include "eosio.system_tester.hpp"

using namespace eosio_system;

BOOST_AUTO_TEST_SUITE(xyz_tests);

// ----------------------------
// test: `transfer`, `swapto`
// ----------------------------
BOOST_FIXTURE_TEST_CASE(transfer_and_swapto, eosio_system_tester) try {
   const std::vector<account_name> accounts = { "alice"_n, "bob"_n };
   create_accounts_with_resources( accounts );
   const account_name alice = accounts[0];
   const account_name bob = accounts[1];

   // fund alice and bob
   // ------------------
   eosio_token.transfer(eos_name, alice, eos("100.0000"));
   eosio_token.transfer(eos_name, bob,   eos("100.0000"));

   // check that we do start with 2.1B XYZ in XYZ's account (`init` action called in deploy_contract)
   // -----------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(get_xyz_balance(xyz_name), xyz("2100000000.0000"));      // initial supply

   // check that you can't send some XYZ you don't have
   // -------------------------------------------------
   BOOST_REQUIRE_EQUAL(get_xyz_balance(alice), xyz("0.0000"));                  // verify no balance
   BOOST_REQUIRE_EQUAL(eosio_xyz.transfer(alice, xyz_name, xyz("1.0000")),
                       error("no balance object found"));

   // swap EOS for XYZ, check that sent EOS was converted to XYZ
   // ----------------------------------------------------------
   BOOST_REQUIRE(check_balances(alice, { eos("100.0000"), xyz("0.0000") }));
   BOOST_REQUIRE_EQUAL(eosio_token.transfer(alice, xyz_name, eos("60.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("40.0000"), xyz("60.0000") }));

   // swap XYZ for EOS, check that sent XYZ was converted to EOS
   // ----------------------------------------------------------
   BOOST_REQUIRE_EQUAL(eosio_xyz.transfer(alice, xyz_name, xyz("10.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("50.0000") }));

   // swap and transfer using `swapto`: convert EOS to XYZ and send to other account
   // ------------------------------------------------------------------------------
   BOOST_REQUIRE(check_balances(bob,   { eos("100.0000"), xyz("0.0000") }));    // Bob has no XYZ
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(alice, bob, eos("5.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("45.0000"),  xyz("50.0000") }));   // Alice spent 5 EOS to send bob 5 XYZ
   BOOST_REQUIRE(check_balances(bob,   { eos("100.0000"), xyz("5.0000") }));    // unchanged EOS balance, received 5 XYZ

   // swap and transfer using `swapto`: convert XYZ to EOS and send to other account
   // let's have Bob return the 5 XYZ that Alice just sent him.
   // ------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(bob, alice, xyz("5.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"),  xyz("50.0000") }));   // Alice got her 5 EOS back
   BOOST_REQUIRE(check_balances(bob,   { eos("100.0000"), xyz("0.0000") }));    // Bob spent his 5 XYZ

   // check that you cannot `swapto` tokens you don't have
   // ----------------------------------------------------
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(alice, bob, eos("150.0000")),
                       error("overdrawn balance"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(bob, alice, xyz("150.0000")),
                       error("overdrawn balance"));
} FC_LOG_AND_RETHROW()

// ----------------------------
// test: `bidname`, `bidrefund`
// ----------------------------
BOOST_FIXTURE_TEST_CASE(bidname, eosio_system_tester) try {
   const std::vector<account_name> accounts = { "alice"_n, "bob"_n };
   create_accounts_with_resources( accounts );
   const account_name alice = accounts[0];
   const account_name bob = accounts[1];

   // fund alice and bob
   // ------------------
   eosio_token.transfer(eos_name, alice, eos("100.0000"));
   eosio_token.transfer(eos_name, bob,   eos("100.0000"));

   // check that we do start with 2.1B XYZ in XYZ's account (`init` action called in deploy_contract)
   // -----------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(get_xyz_balance(xyz_name), xyz("2100000000.0000"));                // initial supply

   // Bid on a name using xyz contract. Convert XYZ to EOS and forward to eos
   // system contract. Must have XYZ balance. Must use XYZ.
   // ----------------------------------------------------------------------
   BOOST_REQUIRE(check_balances(alice, { eos("100.0000"), xyz("0.0000") }));
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, alice, eos("1.0000")),
                       error("Wrong token used"));                                        // Must use XYZ.
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, alice, xyz("1.0000")), 
                       error("no balance object found"));                                 // Must have XYZ balance
   
   BOOST_REQUIRE_EQUAL(eosio_token.transfer(alice, xyz_name, eos("50.0000")), success()); // swap 50 EOS to XYZ
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("50.0000") }));

   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, alice, xyz("1.0000")),
                       error("account already exists"));                                 // Must be new name

   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, "al"_n, xyz("1.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("49.0000") }));

   // Refund bid on a name using xyz contract. Forward refund to eos system
   // contract and swap back refund to XYZ. 
   // ----------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidrefund(alice, "al"_n),                               // In order to get a refund,
                       error("refund not found"));                                       // someone else must bid higher
   BOOST_REQUIRE_EQUAL(eosio_token.transfer(bob, xyz_name, eos("50.0000")), success());  // make sure bob has XYZ
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(bob, "al"_n, xyz("2.0000")), success());        // outbid Alice for name `al`
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidrefund(alice, "al"_n), success());                   // now Alice can get a refund
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("50.0000") }));
   BOOST_REQUIRE(check_balances(bob,   { eos("50.0000"), xyz("48.0000") }));
   
} FC_LOG_AND_RETHROW()


// ----------------------------
// test: `buyram`, 
// ----------------------------
BOOST_FIXTURE_TEST_CASE(ram, eosio_system_tester) try {
   const std::vector<account_name> accounts = { "alice"_n, "bob"_n };
   create_accounts_with_resources( accounts );
   const account_name alice = accounts[0];
   const account_name bob = accounts[1];

   // fund alice and bob
   // ------------------
   eosio_token.transfer(eos_name, alice, eos("100.0000"));
   eosio_token.transfer(eos_name, bob,   eos("100.0000"));

   // check that we do start with 2.1B XYZ in XYZ's account (`init` action called in deploy_contract)
   // -----------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(get_xyz_balance(xyz_name), xyz("2100000000.0000"));              // initial supply

   // buy ram
   // -------
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyram(bob, bob, xyz("0.0000")), error("Swap before amount must be greater than 0"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyram(bob, bob, eos("0.0000")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyram(bob, bob, xyz("1.0000")), error("no balance object found")); 

   // to use the xyz contract, Alice needs to have some XYZ tokens.
   BOOST_REQUIRE_EQUAL(eosio_token.transfer(alice, xyz_name, eos("50.0000")), success()); // swap 50 EOS to XYZ

   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("50.0000") }));            // starting point
   auto ram_before = get_ram_bytes(alice);
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyram(alice, alice, xyz("1.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("49.0000") }));
   auto ram_after_buyram = get_ram_bytes(alice);
   BOOST_REQUIRE_GT(ram_after_buyram, ram_before);

   // buyramburn
   // ----------
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramburn(bob, xyz("0.0000")), error("Swap before amount must be greater than 0"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramburn(bob, eos("0.0000")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramburn(bob, xyz("1.0000")), error("no balance object found"));

   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramburn(alice, xyz("1.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("48.0000") }));
   BOOST_REQUIRE_EQUAL(get_ram_bytes(alice), ram_after_buyram);
   


   
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()