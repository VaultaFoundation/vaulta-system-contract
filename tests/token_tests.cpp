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

BOOST_AUTO_TEST_SUITE(eosio_system_xyz_token_tests);

BOOST_FIXTURE_TEST_CASE( ram_transfer, eosio_system_tester ) try {
   const std::vector<account_name> accounts = { "swapper"_n, "user"_n };
   create_accounts_with_resources( accounts );
   const account_name swapper = accounts[0];
   const account_name user = accounts[1];

   transfer( config::system_account_name, swapper, core_sym::from_string("100.0000"), config::system_account_name );
   transfer( config::system_account_name, user, core_sym::from_string("100.0000"), config::system_account_name );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()