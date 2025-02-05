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

const account_name issuer = "issuer"_n;
const account_name swapper = "swapper"_n;
const account_name hacker = "hacker"_n;
const account_name user = "user"_n;

BOOST_FIXTURE_TEST_CASE( misc, eosio_system_tester ) try {

    const std::vector<account_name> accounts = { issuer, swapper, hacker, user };
    create_accounts_with_resources( accounts );

    // Fill some accounts with EOS so they can swap and test things
    transfer(eos_name, swapper, eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("100.0000"));

    transfer(eos_name, user,   eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(user), eos("100.0000"));

    // check that we do start with 2.1B XYZ in XYZ's account (`init` action called in deploy_contract)
    // -----------------------------------------------------------------------------------------------
    BOOST_REQUIRE_EQUAL(get_xyz_balance(xyz_name), xyz("2100000000.0000"));


    // swap EOS for XYZ, check that sent EOS was converted to XYZ
    // ----------------------------------------------------------
    transfer(swapper, xyz_name, eos("10.0000"), swapper);
    BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("90.0000"));
    BOOST_REQUIRE_EQUAL(get_xyz_balance(swapper), xyz("10.0000"));

    // swap XYZ for EOS, check that sent XYZ was converted to EOS
    // ----------------------------------------------------------
    transfer_xyz(swapper, xyz_name, xyz("9.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("99.0000"));
    BOOST_REQUIRE_EQUAL(get_xyz_balance(swapper), xyz("1.0000"));

    // You should NOT be able to swap EOS you do not have.
    // ---------------------------------------------------
    BOOST_REQUIRE_EXCEPTION(
        transfer(swapper, xyz_name, eos("100.0000"), swapper),
        eosio_assert_message_exception,
        eosio_assert_message_is("overdrawn balance")
    );

    // You should NOT be able to swap XYZ you do not have.
    // ---------------------------------------------------
    BOOST_REQUIRE_EXCEPTION(
        transfer_xyz(swapper, xyz_name, xyz("2.0000")),
        eosio_assert_message_exception,
        eosio_assert_message_is("overdrawn balance")
    );

    // Should be able to swap and withdraw to another account
    // ------------------------------------------------------
    base_tester::push_action( xyz_name, "swapto"_n, swapper, mutable_variant_object()
        ("from",    swapper)
        ("to",      user )
        ("quantity", eos("1.0000"))
        ("memo", "")
    );
    BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("98.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(user), eos("101.0000"));
    BOOST_REQUIRE_EQUAL(get_xyz_balance(swapper), xyz("1.0000"));
    BOOST_REQUIRE_EQUAL(get_xyz_balance(user), xyz("1.0000"));


} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()