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

BOOST_FIXTURE_TEST_CASE( misc, eosio_system_tester ) try {
   const std::vector<account_name> accounts = { "alice"_n, "bob"_n };
   create_accounts_with_resources( accounts );
   const account_name alice = accounts[0];
   const account_name bob = accounts[1];

   // fund alice and bob
   // ------------------
   transfer(eos_name, alice, eos("1000.0000"), eos_name);
   transfer(eos_name, bob,   eos("1000.0000"), eos_name);

   // swap EOS for XYZ, check that sent EOS was converted to XYZ
   // ----------------------------------------------------------
   transfer(alice, xyz_name, eos("100.0000"), alice);
   BOOST_REQUIRE_EQUAL(get_balance(alice, eos_symbol()), eos("900.0000"));
   BOOST_REQUIRE_EQUAL(get_balance(alice, xyz_symbol()), xyz("100.0000"));
   

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()