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
                       error("assertion failure with message: no balance object found"));

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
                       error("assertion failure with message: overdrawn balance"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(bob, alice, xyz("150.0000")),
                       error("assertion failure with message: overdrawn balance"));
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE(automatic_swap_and_use, eosio_system_tester) try {
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

   // bid on a name using xyz contract. Must have XYZ balance. Must use XYZ.
   // ----------------------------------------------------------------------
   BOOST_REQUIRE(check_balances(alice, { eos("100.0000"), xyz("0.0000") }));
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, alice, eos("1.0000")),
                       error("assertion failure with message: Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, alice, xyz("1.0000")), 
                       error("assertion failure with message: no balance object found"));
   
   BOOST_REQUIRE_EQUAL(eosio_token.transfer(alice, xyz_name, eos("50.0000")), success()); // swap 50 EOS to XYZ
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("50.0000") }));

   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, alice, xyz("1.0000")), 
                       error("assertion failure with message: account already exists"));  // need to use new name

   BOOST_REQUIRE_EQUAL(eosio_xyz.bidname(alice, "al"_n, xyz("1.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("49.0000") }));
   
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()