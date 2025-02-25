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
#include "contracts.hpp"

#include "eosio.system_tester.hpp"

using namespace eosio_system;

BOOST_AUTO_TEST_SUITE(xyz_tests);

// ----------------------------
// test: `transfer`, `swapto`
// ----------------------------
BOOST_FIXTURE_TEST_CASE(transfer_and_swapto, eosio_system_tester) try {
   const std::vector<account_name> accounts = { "alice"_n, "bob"_n, "carol"_n };
   create_accounts_with_resources( accounts );
   const account_name alice = accounts[0];
   const account_name bob = accounts[1];
   const account_name carol = accounts[2];

   // fund alice and bob
   // ------------------
   eosio_token.transfer(eos_name, alice, eos("100.0000"));
   eosio_token.transfer(eos_name, bob,   eos("100.0000"));
   eosio_token.transfer(eos_name, carol, eos("100.0000"));

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
   // use `carol` as she has no XYZ to begin with
   // ------------------------------------------------------------------------------
   BOOST_REQUIRE(check_balances(bob,   { eos("100.0000"), xyz("0.0000") }));    // Bob has no XYZ
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(carol, bob, eos("5.0000")), success());
   BOOST_REQUIRE(check_balances(carol, { eos("95.0000"),  xyz("0.0000") }));    // Carol spent 5 EOS to send bob 5 XYZ
   BOOST_REQUIRE(check_balances(bob,   { eos("100.0000"), xyz("5.0000") }));    // unchanged EOS balance, received 5 XYZ

   // swap and transfer using `swapto`: convert XYZ to EOS and send to other account
   // let's have Bob return the 5 XYZ that Carol just sent him.
   // ------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(eosio_xyz.swapto(bob, carol, xyz("5.0000")), success());
   BOOST_REQUIRE(check_balances(carol, { eos("100.0000"),  xyz("0.0000") }));   // Carol got her 5 EOS back
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


// --------------------------------------------------------------------------------
// test: buyram, buyramburn, buyramself, ramburn, buyrambytes, ramtransfer, sellram
// --------------------------------------------------------------------------------
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

   // buyram
   // ------
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

   // buyramself
   // ----------
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramself(bob, xyz("0.0000")), error("Swap before amount must be greater than 0"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramself(bob, eos("0.0000")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramself(bob, xyz("1.0000")), error("no balance object found"));

   BOOST_REQUIRE_EQUAL(eosio_xyz.buyramself(alice, xyz("1.0000")), success());
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("47.0000") }));
   auto ram_after_buyramself = get_ram_bytes(alice);
   BOOST_REQUIRE_GT(ram_after_buyramself, ram_after_buyram);

   // ramburn
   // -------
   BOOST_REQUIRE_EQUAL(eosio_xyz.ramburn(alice, 0), error("cannot reduce negative byte"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.ramburn(alice, 1<<30), error("insufficient quota"));
   
   BOOST_REQUIRE_EQUAL(eosio_xyz.ramburn(alice, ram_after_buyramself - ram_after_buyram), success());
   BOOST_REQUIRE_EQUAL(get_ram_bytes(alice), ram_after_buyram);
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz("47.0000") }));

   // buyrambytes
   // -----------
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrambytes(bob, bob, 1024), error("no balance object found"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrambytes(bob, bob, 0), error("Swap before amount must be greater than 0"));

   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrambytes(alice, alice, 1024), success());
   auto ram_bought = get_ram_bytes(alice) - ram_after_buyram;
   BOOST_REQUIRE_EQUAL(ram_bought, 1017);                     // looks like we don't get the exact requested amount

   auto xyz_after_buyrambytes = get_xyz_balance(alice);       // we don't know exactly how much we spent
   BOOST_REQUIRE_LT(xyz_after_buyrambytes, xyz("47.0000"));   // but it must be > 0
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000") }));  // and EOS balance should be unchanged

   // ramtransfer
   // -----------
   auto bob_ram_before_transfer = get_ram_bytes(bob);
   BOOST_REQUIRE_EQUAL(eosio_xyz.ramtransfer(alice, bob, ram_bought), success());
   BOOST_REQUIRE_EQUAL(get_ram_bytes(alice), ram_after_buyram);
   BOOST_REQUIRE_EQUAL(get_ram_bytes(bob), bob_ram_before_transfer + ram_bought);
   BOOST_REQUIRE(check_balances(alice, { eos("50.0000"), xyz_after_buyrambytes }));

   // sellram
   // -------
   auto bob_ram_before_sell = get_ram_bytes(bob);
   auto [bob_eos_before_sell, bob_xyz_before_sell] = std::pair{ get_eos_balance(bob),  get_xyz_balance(bob)};
   BOOST_REQUIRE_EQUAL(eosio_xyz.sellram(bob, ram_bought), success());
   BOOST_REQUIRE_EQUAL(get_ram_bytes(bob), bob_ram_before_sell - ram_bought);
   BOOST_REQUIRE_EQUAL(get_eos_balance(bob),  bob_eos_before_sell);  // no change, proceeds swapped for XYZ
   BOOST_REQUIRE_GT(get_xyz_balance(bob), bob_xyz_before_sell);      // proceeds of sellram 
} FC_LOG_AND_RETHROW()


// --------------------------------------------------------------------------------
// tested: deposit, buyrex, withdraw, delegatebw,undelegatebw, refund
// no comprehensive tests needed as direct forwarding: sellrex, mvtosavings, mvfrsavings, 
// --------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(rex_tests, eosio_system_tester) try {
   const std::vector<account_name> accounts = { "alice"_n, "bob"_n };
   create_accounts_with_resources( accounts );
   const account_name alice = accounts[0];
   const account_name bob   = accounts[1];

   // fund alice and bob
   // ------------------
   eosio_token.transfer(eos_name, alice, eos("100.0000"));
   eosio_token.transfer(eos_name, bob,   eos("100.0000"));

   // check that we do start with 2.1B XYZ in XYZ's account (`init` action called in deploy_contract)
   // -----------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(get_xyz_balance(xyz_name), xyz("2100000000.0000"));              // initial supply

   // deposit
   // ------
   BOOST_REQUIRE_EQUAL(eosio_xyz.deposit(bob, xyz("0.0000")), error("Swap before amount must be greater than 0"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.deposit(bob, eos("0.0000")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.deposit(bob, xyz("1.0000")), error("no balance object found"));

   // to use the xyz contract, Bob needs to have some XYZ tokens.
   BOOST_REQUIRE_EQUAL(eosio_token.transfer(bob, xyz_name, eos("50.0000")), success()); // swap 50 EOS to XYZ
   BOOST_REQUIRE_EQUAL(eosio_xyz.deposit(bob, xyz("10.0000")), success());

   // buyrex
   // ------
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrex(bob, eos("1.0000")), error("Wrong token used")); 
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrex(bob, asset::from_string("1.0000 BOGUS")), error("Wrong token used")); 
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrex(bob, xyz("0.0000")), error("must use positive amount"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrex(bob, xyz("-1.0000")), error("must use positive amount"));
   
   BOOST_REQUIRE_EQUAL(eosio_xyz.buyrex(bob, xyz("2.0000")), success());
   BOOST_REQUIRE_EQUAL(get_rex_balance(bob), rex(20000'0000u));

   // mvtosavings
   // -----------
   BOOST_REQUIRE_EQUAL(eosio_xyz.mvtosavings(bob, rex(20000'0000u)), success()); 

   // mvfrsavings
   // -----------
   BOOST_REQUIRE_EQUAL(eosio_xyz.mvfrsavings(bob, rex(20000'0000u)), success());
   
   // sellrex
   // ------
   BOOST_REQUIRE_EQUAL(eosio_xyz.sellrex(bob, eos("0.0000")), error("asset must be a positive amount of (REX, 4)"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.sellrex(bob, xyz("-1.0000")), error("asset must be a positive amount of (REX, 4)"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.sellrex(bob, xyz("1.0000")), error("asset must be a positive amount of (REX, 4)"));

   BOOST_REQUIRE_EQUAL(eosio_xyz.sellrex(bob, rex(20000'0000u)), error("insufficient available rex")); 
   produce_block( fc::days(30) ); // must wait
   BOOST_REQUIRE_EQUAL(eosio_xyz.sellrex(bob, rex(20000'0000u)), success());

   // withdraw
   // --------
   BOOST_REQUIRE_EQUAL(eosio_xyz.withdraw(bob, eos("1.0000")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.withdraw(bob, asset::from_string("5.0000 BOGUS")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.withdraw(bob, xyz("11.0000")), error("insufficient funds")); // we deposited only 10 XYZ
   
   BOOST_REQUIRE_EQUAL(eosio_xyz.withdraw(bob, xyz("5.0000")), success());
   BOOST_REQUIRE_EQUAL(get_xyz_balance(bob), xyz("45.0000"));               // check that it got converted back into XYZ

   BOOST_REQUIRE_EQUAL(eosio_xyz.withdraw(bob, xyz("5.0000")), success());
   BOOST_REQUIRE_EQUAL(get_xyz_balance(bob), xyz("50.0000"));               // check that it got converted back into XYZ

   // delegatebw
   // ----------
   auto old_balance = get_xyz_balance(bob);
   transfer(eos_name, bob, eos("100000.0000"));
   transfer(bob, xyz_name, eos("100000.0000"), bob);
   active_and_vote_producers();

   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, xyz("0.0000"), xyz("0.0000"), false),
                       error("Swap before amount must be greater than 0"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, xyz("2.0000"), xyz("-1.0000"), false),
                       error("must stake a positive amount"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, xyz("-1.0000"), xyz("2.0000"), false),
                       error("must stake a positive amount"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, eos("1.0000"), xyz("2.0000"), false),
                       error("attempt to add asset with different symbol"));
   auto bogus_asset = asset::from_string("1.0000 BOGUS");
   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, bogus_asset, bogus_asset, false),
                       error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, xyz("1.0000"), xyz("100000.0000"), true),
                       error("cannot use transfer flag if delegating to self"));

   BOOST_REQUIRE_EQUAL(eosio_xyz.delegatebw(bob, bob, xyz("1.0000"), xyz("100000.0000"), false), success());
   BOOST_REQUIRE_EQUAL(get_xyz_balance(bob), old_balance - xyz("1.0000"));

   // undelegatebw
   // ------------
   BOOST_REQUIRE_EQUAL(eosio_xyz.refund(bob), error("refund request not found")); // have to undelegatebw first
   BOOST_REQUIRE_EQUAL(eosio_xyz.undelegatebw(bob, bob, xyz("0.0000"), bogus_asset), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.undelegatebw(bob, bob, bogus_asset, xyz("0.0000")), error("Wrong token used"));
   BOOST_REQUIRE_EQUAL(eosio_xyz.undelegatebw(bob, bob, xyz("0.0000"), xyz("0.0000")),
                       error("must unstake a positive amount"));
   
   BOOST_REQUIRE_EQUAL(eosio_xyz.undelegatebw(bob, bob, xyz("0.0000"), xyz("1.0000")), success());

   // refund
   // ------
   BOOST_REQUIRE_EQUAL(eosio_xyz.refund(bob), error("refund is not available yet"));
   produce_block( fc::days(10) );
   BOOST_REQUIRE_EQUAL(eosio_xyz.refund(bob), success());
   BOOST_REQUIRE_EQUAL(get_xyz_balance(bob), old_balance);

} FC_LOG_AND_RETHROW()


const account_name issuer = "issuer"_n;
const account_name swapper = "swapper"_n;
const account_name hacker = "hacker"_n;
const account_name user = "user"_n;
const account_name user2 = "user2"_n;
const account_name user3 = "user3"_n;

BOOST_FIXTURE_TEST_CASE( misc, eosio_system_tester ) try {


    const std::vector<account_name> accounts = { issuer, swapper, hacker, user, user2, user3 };
    create_accounts_with_resources( accounts );
    produce_block();

    // Fill some accounts with EOS so they can swap and test things
    transfer(eos_name, swapper, eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("100.0000"));

    transfer(eos_name, user,   eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(user), eos("100.0000"));
    transfer(eos_name, user2,   eos("100.0000"));
    transfer(eos_name, user3,   eos("100.0000"));

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
    BOOST_REQUIRE_EQUAL(get_balance(user), eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_xyz_balance(swapper), xyz("1.0000"));
    BOOST_REQUIRE_EQUAL(get_xyz_balance(user), xyz("1.0000"));

    // swap some EOS to XYZ
    transfer(user, xyz_name, eos("50.0000"), user);
    transfer(user2, xyz_name, eos("50.0000"), user2);
    transfer(user3, xyz_name, eos("50.0000"), user3);

    // Should be able to automatically swap tokens and use system contracts
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "bidname"_n, swapper, mutable_variant_object()
            ("bidder",    user)
            ("newname",   "newname")
            ("bid",       xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
    }

    // Should be able to bidrefund
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "bidname"_n, user2, mutable_variant_object()
            ("bidder",    user2)
            ("newname",   "newname")
            ("bid",       xyz("1.5000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance);

        base_tester::push_action( xyz_name, "bidrefund"_n, user, mutable_variant_object()
            ("bidder",    user)
            ("newname",   "newname")
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance + xyz("1.0000"));
    }

    // Should be able to buyram
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "buyram"_n, user, mutable_variant_object()
            ("payer",    user)
            ("receiver", user)
            ("quant", xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
    }

    // Should be able to buyramself
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "buyramself"_n, user, mutable_variant_object()
            ("payer",    user)
            ("quant", xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
    }

    // Should be able to buyramburn
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "buyramburn"_n, user, mutable_variant_object()
            ("payer",    user)
            ("quantity", xyz("1.0000"))
            ("memo", std::string("memo"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
    }

    // Should be able to buyrambytes
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "buyrambytes"_n, user, mutable_variant_object()
            ("payer",    user)
            ("receiver", user)
            ("bytes", 1024)
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user) < old_balance, true);
    }

    // Should be able to burnram
    {
        base_tester::push_action( xyz_name, "ramburn"_n, user, mutable_variant_object()
            ("owner",    user)
            ("bytes", 10)
            ("memo", "memo")
        );
    }

    // Should be able to sellram
    {
        auto old_balance = get_xyz_balance(user);
        auto old_balance_eos = get_balance(user);
        base_tester::push_action( xyz_name, "sellram"_n, user, mutable_variant_object()
            ("account",    user)
            ("bytes", 1024)
        );

        BOOST_REQUIRE_EQUAL(get_balance(user), old_balance_eos);
        BOOST_REQUIRE_EQUAL(get_xyz_balance(user) > old_balance, true);
    }

    // should be able to stake to rex
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "deposit"_n, user, mutable_variant_object()
            ("owner",    user)
            ("amount", xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));

        auto rex_fund = get_rex_fund(user);
        BOOST_REQUIRE_EQUAL(rex_fund, eos("1.0000"));

        base_tester::push_action( xyz_name, "buyrex"_n, user, mutable_variant_object()
            ("from",    user)
            ("amount", xyz("1.0000"))
        );

        auto rex_balance = get_rex_balance(user);
        BOOST_REQUIRE_EQUAL(rex_balance, rex(10000'0000));
    }

    // should be able to unstake from rex
    {
        base_tester::push_action( "eosio"_n, "mvtosavings"_n, user, mutable_variant_object()
            ("owner",    user)
            ("rex", rex(10000'0000))
        );
        base_tester::push_action( xyz_name, "mvfrsavings"_n, user, mutable_variant_object()
            ("owner",    user)
            ("rex", rex(10000'0000))
        );

        produce_block();
        produce_block( fc::days(30) );

        // sell rex
        base_tester::push_action( xyz_name, "sellrex"_n, user, mutable_variant_object()
            ("from",    user)
            ("rex", rex(10000'0000))
        );
    }

    // should be able to withdraw
    {
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "withdraw"_n, user, mutable_variant_object()
            ("owner",    user)
            ("amount", xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance + xyz("1.0000"));
    }


    // should be able to donate to rex
    {
        // need to buy back in, as rex is no longer initialized
        {            base_tester::push_action( xyz_name, "deposit"_n, user, mutable_variant_object()
                ("owner",    user)
                ("amount", xyz("1.0000"))
            );

            base_tester::push_action( xyz_name, "buyrex"_n, user, mutable_variant_object()
                ("from",    user)
                ("amount", xyz("1.0000"))
            );
        }

        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "donatetorex"_n, user, mutable_variant_object()
            ("payer",    user)
            ("quantity", xyz("1.0000"))
            ("memo", "")
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));

        // cannot donate with EOS
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "donatetorex"_n, user, mutable_variant_object()
                ("payer",    user)
                ("quantity", eos("1.0000"))
                ("memo", "")
            ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Wrong token used")
        );

        // cannot donate with wrong account
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "donatetorex"_n, user, mutable_variant_object()
                ("payer",    user2)
                ("quantity", xyz("1.0000"))
                ("memo", "")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user2")

        );
    }


    transfer(eos_name, user, eos("100000.0000"));
    transfer(user, xyz_name, eos("100000.0000"), user);
    vector<name> producers = active_and_vote_producers();

    // should be able to powerup and get overages back in XYZ
    // TODO: Powerup isn't initialized
//     {
//         auto old_balance = get_xyz_balance(user);
//         base_tester::push_action( xyz_name, "powerup"_n, user, mutable_variant_object()
//             ("payer",    user)
//             ("receiver", user)
//             ("days", 30)
//             ("net_frac", 1)
//             ("cpu_frac", 1)
//             ("max_payment", xyz("1.0000"))
//         );
//
//         BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
//     }

    // should be able to delegate and undelegate bw
    {
        auto old_balance = get_xyz_balance(user) - xyz("100000.0000");
        std::cout << "old_balance: " << old_balance << std::endl;
        base_tester::push_action( xyz_name, "delegatebw"_n, user, mutable_variant_object()
            ("from",    user)
            ("receiver", user)
            ("stake_net_quantity", xyz("1.0000"))
            ("stake_cpu_quantity", xyz("100000.0000"))
            ("transfer", false)
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));

        base_tester::push_action( xyz_name, "undelegatebw"_n, user, mutable_variant_object()
            ("from",    user)
            ("receiver", user)
            ("unstake_net_quantity", xyz("0.0000"))
            ("unstake_cpu_quantity", xyz("1.0000"))
        );

        produce_block();
        produce_block( fc::days(10) );

        base_tester::push_action( xyz_name, "refund"_n, user, mutable_variant_object()
            ("owner",    user)
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance);

    }

    // claimrewards
    {
        auto producer = producers[0];
        auto old_balance = get_xyz_balance(producer);
        base_tester::push_action( xyz_name, "claimrewards"_n, producer, mutable_variant_object()
            ("owner",    producer)
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(producer) > old_balance, true);
    }

    // linkauth
    {
        base_tester::push_action( xyz_name, "linkauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("code",       xyz_name)
            ("type",       "transfer"_n)
            ("requirement", "active"_n)
        );
    }

    // unlinkauth
    {
        base_tester::push_action( xyz_name, "unlinkauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("code",       xyz_name)
            ("type",       "transfer"_n)
        );
    }

    // updateauth and deleteauth
    {
        base_tester::push_action( xyz_name, "updateauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("permission", "test"_n)
            ("parent",     "active"_n)
            ("auth",       authority(1, {key_weight{get_public_key(user, "active"), 1}}))
        );

        base_tester::push_action( xyz_name, "deleteauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("permission", "test"_n)
        );
    }

    // setcode and setabi
    {
        // create contract account
        name contract_account = "contractest"_n;
        create_accounts_with_resources( { contract_account } );

        // get some CPU and NET with delegatebw
        base_tester::push_action( eos_name, "delegatebw"_n, eos_name, mutable_variant_object()
            ("from",    eos_name)
            ("receiver", contract_account)
            ("stake_net_quantity", eos("10.0000"))
            ("stake_cpu_quantity", eos("500.0000"))
            ("transfer", false)
        );
//
        base_tester::push_action( eos_name, "buyram"_n, eos_name, mutable_variant_object()
            ("payer",    eos_name)
            ("receiver", contract_account)
            ("quant", eos("1000000.0000"))
        );

//         base_tester::push_action( xyz_name, "setcode"_n, contract_account, mutable_variant_object()
//             ("account",    contract_account)
//             ("vmtype",     0)
//             ("vmversion",  0)
//             ("code",       eos_contracts::token_wasm() )
//         );

//         base_tester::push_action( xyz_name, "setabi"_n, contract_account, mutable_variant_object()
//             ("account",    contract_account)
//             ("abi",        eos_contracts::token_abi().data() )
//         );

    }


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()