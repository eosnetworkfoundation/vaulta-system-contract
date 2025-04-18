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

std::vector<char> prepare_wasm(const std::vector<uint8_t>& uint8_vector) {
    std::vector<char> char_vector(uint8_vector.size());
    std::copy(uint8_vector.begin(), uint8_vector.end(), char_vector.begin());
    return char_vector;
}

inline constexpr int64_t powerup_frac  = 1'000'000'000'000'000ll; // 1.0 = 10^15
inline constexpr int64_t stake_weight = 100'000'000'0000ll; // 10^12

struct powerup_config_resource {
   std::optional<int64_t>        current_weight_ratio = {};
   std::optional<int64_t>        target_weight_ratio  = {};
   std::optional<int64_t>        assumed_stake_weight = {};
   std::optional<time_point_sec> target_timestamp     = {};
   std::optional<double>         exponent             = {};
   std::optional<uint32_t>       decay_secs           = {};
   std::optional<asset>          min_price            = {};
   std::optional<asset>          max_price            = {};
};
FC_REFLECT(powerup_config_resource,                                                             //
           (current_weight_ratio)(target_weight_ratio)(assumed_stake_weight)(target_timestamp) //
           (exponent)(decay_secs)(min_price)(max_price))

struct powerup_config {
   powerup_config_resource net             = {};
   powerup_config_resource cpu             = {};
   std::optional<uint32_t> powerup_days    = {};
   std::optional<asset>    min_powerup_fee = {};
};
FC_REFLECT(powerup_config, (net)(cpu)(powerup_days)(min_powerup_fee))


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
const account_name user4 = "user4"_n;
const account_name user5 = "user5"_n;
const account_name exchange = "exchange"_n;
const account_name powerupuser = "powuser"_n;
const vector<account_name> swapram_accounts = { "swapram1"_n, "swapram2"_n, "swapram3"_n, "swapram4"_n, "swapram5"_n };
const vector<account_name> swaptoram_accounts = { "swaptoram1"_n, "swaptoram2"_n, "swaptoram3"_n, "swaptoram4"_n, "swaptoram5"_n };
const vector<account_name> swaptoram_receivers = { "rswaptoram1"_n, "srwaptoram2"_n, "rswaptoram3"_n, "srwaptoram4"_n, "rswaptoram5"_n };

BOOST_FIXTURE_TEST_CASE( misc, eosio_system_tester ) try {


    const std::vector<account_name> accounts = { issuer, swapper, hacker, user, user2, user3, user4, user5, exchange, powerupuser, "eosio.reserv"_n };
    create_accounts_with_resources( accounts );
    create_accounts_with_resources( swapram_accounts );
    create_accounts_with_resources( swaptoram_accounts );
    create_accounts_with_resources( swaptoram_receivers );
    produce_block();

    // Fill some accounts with EOS so they can swap and test things
    transfer(eos_name, swapper, eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("100.0000"));

    transfer(eos_name, user,   eos("100.0000"));
    BOOST_REQUIRE_EQUAL(get_balance(user), eos("100.0000"));
    transfer(eos_name, user2,   eos("100.0000"));
    transfer(eos_name, user3,   eos("100.0000"));
    transfer(eos_name, user4,   eos("100.0000"));
    transfer(eos_name, user5,   eos("100.0000"));
    for (auto a : swapram_accounts) {
        transfer(eos_name, a, eos("100.0000"));
    }
    for (auto a : swaptoram_accounts) {
        transfer(eos_name, a, eos("100.0000"));
    }
    transfer(eos_name, swaptoram_receivers[0], eos("100.0000"));
    transfer(eos_name, swaptoram_receivers[1], eos("100.0000"));

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

    // check that an account can block themselves from receiving swapto
    // ----------------------------------------------------------------
    // can swapto to the account
    {
        base_tester::push_action( xyz_name, "swapto"_n, swapper, mutable_variant_object()
            ("from",    swapper)
            ("to",      exchange )
            ("quantity", eos("1.0000"))
            ("memo", "")
        );
        BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("97.0000"));
        BOOST_REQUIRE_EQUAL(get_balance(user), eos("100.0000"));
        BOOST_REQUIRE_EQUAL(get_xyz_balance(swapper), xyz("1.0000"));
        BOOST_REQUIRE_EQUAL(get_xyz_balance(exchange), xyz("1.0000"));
        produce_block();
    }
    // can block the recipient and swapto will fail
    {
        base_tester::push_action( xyz_name, "blockswapto"_n, exchange, mutable_variant_object()
            ("account",    exchange)
            ("block",      true)
        );
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "swapto"_n, swapper, mutable_variant_object()
                ("from",    swapper)
                ("to",      exchange )
                ("quantity", eos("1.0000"))
                ("memo", "")
            ),
            eosio_assert_message_exception,
            eosio_assert_message_is("Recipient is blocked from receiving swapped tokens: " + exchange.to_string())
        );
        produce_block();
    }

    // can unblock and swapto
    {
        base_tester::push_action( xyz_name, "blockswapto"_n, exchange, mutable_variant_object()
            ("account",    exchange)
            ("block",      false)
        );
        base_tester::push_action( xyz_name, "swapto"_n, swapper, mutable_variant_object()
            ("from",    swapper)
            ("to",      exchange )
            ("quantity", eos("1.0000"))
            ("memo", "")
        );
        BOOST_REQUIRE_EQUAL(get_balance(swapper), eos("96.0000"));
        BOOST_REQUIRE_EQUAL(get_balance(user), eos("100.0000"));
        BOOST_REQUIRE_EQUAL(get_xyz_balance(swapper), xyz("1.0000"));
        BOOST_REQUIRE_EQUAL(get_xyz_balance(exchange), xyz("2.0000"));
    }

    // can block from the contract itself
    {
        base_tester::push_action( xyz_name, "blockswapto"_n, xyz_name, mutable_variant_object()
            ("account",    exchange)
            ("block",      true)
        );
        base_tester::push_action( xyz_name, "blockswapto"_n, xyz_name, mutable_variant_object()
            ("account",    exchange)
            ("block",      false)
        );
        produce_block();
        // and can always unblock yourself
        base_tester::push_action( xyz_name, "blockswapto"_n, xyz_name, mutable_variant_object()
            ("account",    exchange)
            ("block",      true)
        );
        base_tester::push_action( xyz_name, "blockswapto"_n, exchange, mutable_variant_object()
            ("account",    exchange)
            ("block",      false)
        );
        produce_block();
    }

    // should never be able to add to a blocklist if not one of those three accounts
    {
        // catch missing auth exception
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "blockswapto"_n, user, mutable_variant_object()
                ("account",    exchange)
                ("block",      true)
            ),
            missing_auth_exception,
            fc_exception_message_starts_with("missing authority of ")
        );
    }

    // can not swapto with tokens you do not own
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "swapto"_n, user, mutable_variant_object()
                ("from",    user2)
                ("to",      exchange )
                ("quantity", eos("1.0000"))
                ("memo", "")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user2")
        );
    }

    // should consume the contract's RAM when swapping from a new account using transfer
    {
        // buy ram for xyz account
        {
            base_tester::push_action( eos_name, "buyram"_n, eos_name, mutable_variant_object()
                ("payer",    eos_name)
                ("receiver", xyz_name)
                ("quant", eos("2000000.0000"))
            );
        }

        // the users haven't paid the ram for their own tokens yet because they haven't touched
        // them yet, so we're going to make the user take ram ownership of their row on eosio.token
        for (auto a : swapram_accounts) {
            transfer(a, user, eos("1.0000"), a);
        }
        for (auto a : swaptoram_accounts) {
            transfer(a, user, eos("1.0000"), a);
        }
        transfer(swaptoram_receivers[0], user, eos("1.0000"), swaptoram_receivers[0]);
        transfer(swaptoram_receivers[1], user, eos("1.0000"), swaptoram_receivers[1]);

        struct ram_data {
            name account;
            int64_t swap_user_delta;
            int64_t swap_xyz_delta;
            int64_t swap_eos_delta;
            int64_t transfer_user_delta;
            int64_t transfer_xyz_delta;
        };

        auto test_swap_ram = [&](const account_name account) {
            ram_data data;
            data.account = account;
            {
                auto eos_ram_before = get_account_ram(eos_name);
                auto xyz_ram_before = get_account_ram(xyz_name);
                auto user_ram_before = get_account_ram(account);

                transfer(account, xyz_name, eos("10.0000"), account);

                auto eos_ram_after = get_account_ram(eos_name);
                auto xyz_ram_after = get_account_ram(xyz_name);
                auto user_ram_after = get_account_ram(account);

                data.swap_user_delta = user_ram_after - user_ram_before;
                data.swap_xyz_delta = xyz_ram_after - xyz_ram_before;
                data.swap_eos_delta = eos_ram_after - eos_ram_before;
            }

            {
                auto xyz_ram_before = get_account_ram(xyz_name);
                auto user_ram_before = get_account_ram(account);
                transfer_xyz(account, user, xyz("1.0000"));
                auto xyz_ram_after = get_account_ram(xyz_name);
                auto user_ram_after = get_account_ram(account);

                data.transfer_user_delta = user_ram_after - user_ram_before;
                data.transfer_xyz_delta = xyz_ram_after - xyz_ram_before;
            }


            produce_block();

            return data;
        };

        {
            for (auto a : swapram_accounts) {

                auto results = test_swap_ram(a);

                // When swapping, the xyz contract pays for the RAM
                BOOST_REQUIRE_EQUAL(results.swap_user_delta, 0);
                BOOST_REQUIRE_EQUAL(results.swap_xyz_delta, -241); // 240 bytes of RAM used by the xyz contract
                BOOST_REQUIRE_EQUAL(results.swap_eos_delta, 0);

                // Upon first transfer, the user should take RAM ownership and release the xyz contract's RAM
                BOOST_REQUIRE_EQUAL(results.transfer_user_delta, -241);
                BOOST_REQUIRE_EQUAL(results.transfer_xyz_delta, 241);
            }
        }

        // There should be no changes in RAM this time as the user already pays for their rows
        {
            for (auto a : swapram_accounts) {

                auto results = test_swap_ram(a);

                BOOST_REQUIRE_EQUAL(results.swap_user_delta, 0);
                BOOST_REQUIRE_EQUAL(results.swap_xyz_delta, 0);
                BOOST_REQUIRE_EQUAL(results.swap_eos_delta, 0);
                BOOST_REQUIRE_EQUAL(results.transfer_user_delta, 0);
                BOOST_REQUIRE_EQUAL(results.transfer_xyz_delta, 0);
            }
        }
    }

    // should consume ram the same way with swapto
    {
        struct ram_data {
            name from;
            name to;
            int64_t swapto_from_delta;
            int64_t swapto_to_delta;
            int64_t swapto_xyz_delta;
            int64_t swapto_eos_delta;

            int64_t transfer_from_delta;
            int64_t transfer_to_delta;
            int64_t transfer_xyz_delta;
        };

        auto test_swapto_ram = [&](const account_name from, const account_name to) {
            ram_data data;
            data.from = from;
            data.to = to;

            {
                auto eos_ram_before = get_account_ram(eos_name);
                auto xyz_ram_before = get_account_ram(xyz_name);
                auto from_ram_before = get_account_ram(from);
                auto to_ram_before = get_account_ram(to);

                base_tester::push_action( xyz_name, "swapto"_n, from, mutable_variant_object()
                    ("from",    from)
                    ("to",      to )
                    ("quantity", eos("1.0000"))
                    ("memo", "")
                );

                auto eos_ram_after = get_account_ram(eos_name);
                auto xyz_ram_after = get_account_ram(xyz_name);
                auto from_ram_after = get_account_ram(from);
                auto to_ram_after = get_account_ram(to);

                data.swapto_from_delta = from_ram_after - from_ram_before;
                data.swapto_to_delta = to_ram_after - to_ram_before;
                data.swapto_xyz_delta = xyz_ram_after - xyz_ram_before;
                data.swapto_eos_delta = eos_ram_after - eos_ram_before;

            }

            // check on first transfer
            {
                auto xyz_ram_before = get_account_ram(xyz_name);
                auto from_ram_before = get_account_ram(from);
                auto to_ram_before = get_account_ram(to);
                transfer_xyz(to, user, xyz("1.0000"));
                auto xyz_ram_after = get_account_ram(xyz_name);
                auto from_ram_after = get_account_ram(from);
                auto to_ram_after = get_account_ram(to);

                data.transfer_from_delta = from_ram_after - from_ram_before;
                data.transfer_to_delta = to_ram_after - to_ram_before;
                data.transfer_xyz_delta = xyz_ram_after - xyz_ram_before;

            }

            produce_block();

            return data;
        };

        {
            auto results = test_swapto_ram(swaptoram_accounts[0], swaptoram_receivers[0]);
            // This is the first time this account has swapped, so it should pay for the RAM for itself
            // because it is also transferring within the same transaction
            BOOST_REQUIRE_EQUAL(results.swapto_from_delta, -241);

            // The receiver should not pay for the RAM because it is the first time it has received tokens
            BOOST_REQUIRE_EQUAL(results.swapto_to_delta, 0);

            // The xyz contract should pay for the RAM for the receiver
            BOOST_REQUIRE_EQUAL(results.swapto_xyz_delta, -241);

            // then once the receiver transfers tokens the first time it should pay for the RAM
            BOOST_REQUIRE_EQUAL(results.transfer_from_delta, 0);
            BOOST_REQUIRE_EQUAL(results.transfer_to_delta, -241);
            BOOST_REQUIRE_EQUAL(results.transfer_xyz_delta, 241);
        }

        {
            auto results = test_swapto_ram(swaptoram_accounts[1], swaptoram_receivers[0]);

            // This is the first time this account has swapped, so it should pay for the RAM for itself
            // because it is also transferring within the same transaction
            BOOST_REQUIRE_EQUAL(results.swapto_from_delta, -241);

            // But now no one else pays anything because the receiver has already paid for their RAM in the
            // previous transaction, and the contract was never a part of ram payment here
            BOOST_REQUIRE_EQUAL(results.swapto_to_delta, 0);
            BOOST_REQUIRE_EQUAL(results.swapto_xyz_delta, 0);
            BOOST_REQUIRE_EQUAL(results.swapto_eos_delta, 0);
            BOOST_REQUIRE_EQUAL(results.transfer_from_delta, 0);
            BOOST_REQUIRE_EQUAL(results.transfer_to_delta, 0);
        }

        {
            // This is the same as the first swapto test, because it's from a new sender to a new receiver.
            // No need to test again.
            test_swapto_ram(swaptoram_accounts[2], swaptoram_receivers[2]);

            auto results = test_swapto_ram(swaptoram_accounts[2], swaptoram_receivers[3]);

            // This sender now no longer pays anything because they already have a row.
            BOOST_REQUIRE_EQUAL(results.swapto_from_delta, 0);

            // Receiver still pays nothing
            BOOST_REQUIRE_EQUAL(results.swapto_to_delta, 0);

            // The contract still pays for the receiver
            BOOST_REQUIRE_EQUAL(results.swapto_xyz_delta, -241);

            // The receiver pays for their own RAM
            BOOST_REQUIRE_EQUAL(results.transfer_from_delta, 0);
            BOOST_REQUIRE_EQUAL(results.transfer_to_delta, -241);
            BOOST_REQUIRE_EQUAL(results.transfer_xyz_delta, 241);
        }

        {
            auto results = test_swapto_ram(swaptoram_accounts[2], swaptoram_receivers[4]);

            // sanity check to make sure the same happens as above on subsequent swaps
            BOOST_REQUIRE_EQUAL(results.swapto_from_delta, 0);
            BOOST_REQUIRE_EQUAL(results.swapto_to_delta, 0);
            BOOST_REQUIRE_EQUAL(results.swapto_xyz_delta, -241);
            BOOST_REQUIRE_EQUAL(results.swapto_eos_delta, 0);
            BOOST_REQUIRE_EQUAL(results.transfer_from_delta, 0);
            BOOST_REQUIRE_EQUAL(results.transfer_to_delta, -241);
            BOOST_REQUIRE_EQUAL(results.transfer_xyz_delta, 241);
        }
    }

   // Users opening a new XYZ balance should be prereleased if opening for themselves
   {
        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user3), -1);
        auto xyz_ram_before = get_account_ram(xyz_name);
        auto user_ram_before = get_account_ram(user3);

        base_tester::push_action( xyz_name, "open"_n, user3, mutable_variant_object()
            ("owner",    user3)
            ("symbol",   xyz_symbol())
            ("ram_payer", user3)
        );

        auto xyz_ram_after = get_account_ram(xyz_name);
        auto user_ram_after = get_account_ram(user3);

        BOOST_REQUIRE_EQUAL(xyz_ram_after - xyz_ram_before, 0);
        BOOST_REQUIRE_EQUAL(user_ram_after - user_ram_before, -241);

        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user3), 1);
   }

   // User opening a balance for another is not prereleased
   {
        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user4), -1);
        auto xyz_ram_before = get_account_ram(xyz_name);
        auto user_ram_before = get_account_ram(user3);

        base_tester::push_action( xyz_name, "open"_n, user3, mutable_variant_object()
            ("owner",    user4)
            ("symbol",   xyz_symbol())
            ("ram_payer", user3)
        );

        auto xyz_ram_after = get_account_ram(xyz_name);
        auto user_ram_after = get_account_ram(user3);

        BOOST_REQUIRE_EQUAL(xyz_ram_after - xyz_ram_before, 0);
        BOOST_REQUIRE_EQUAL(user_ram_after - user_ram_before, -241);

        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user4), 0);
   }

   // Giving user4 some xyz, but ram stays the same for all parties
   {
        // making sure that user has already paid for its own row
        transfer_xyz(user, eos_name, xyz("1.0000"));

        auto user_ram_before = get_account_ram(user);
        auto user4_ram_before = get_account_ram(user4);
        transfer_xyz(user, user4, xyz("1.0000"));
        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user4), 0);
        auto user_ram_after = get_account_ram(user);
        auto user4_ram_after = get_account_ram(user4);

        BOOST_REQUIRE_EQUAL(user_ram_after - user_ram_before, 0);
        BOOST_REQUIRE_EQUAL(user4_ram_after - user4_ram_before, 0);
   }

    // On user4 first transfer, user3 gets ram back
    {
        auto user_ram_before = get_account_ram(user3);
        auto user4_ram_before = get_account_ram(user4);
        transfer_xyz(user4, user, xyz("1.0000"));
        auto user_ram_after = get_account_ram(user3);
        auto user4_ram_after = get_account_ram(user4);

        BOOST_REQUIRE_EQUAL(user_ram_after - user_ram_before, 241);
        BOOST_REQUIRE_EQUAL(user4_ram_after - user4_ram_before, -241);

        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user4), 1);
    }

    // when doing swapto, account is not prereleased
    {
        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user5), -1);

        base_tester::push_action( xyz_name, "swapto"_n, user, mutable_variant_object()
            ("from",    user)
            ("to",      user5)
            ("quantity", eos("1.0000"))
            ("memo", "")
        );

        BOOST_REQUIRE_EQUAL(get_xyz_account_released(user5), 0);
    }




//     transfer(user3, xyz_name, eos("50.0000"), user3);


    // swap some EOS to XYZ
    transfer(user, xyz_name, eos("50.0000"), user);
    transfer(user2, xyz_name, eos("50.0000"), user2);
    transfer(eos_name, powerupuser, eos("100000.0000"));
    transfer(powerupuser, xyz_name, eos("100000.0000"), powerupuser);

    // Should be able to automatically swap tokens and use system contracts
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "bidname"_n, user2, mutable_variant_object()
                ("bidder",    user)
                ("newname",   "newname")
                ("bid",       xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "bidname"_n, user, mutable_variant_object()
            ("bidder",    user)
            ("newname",   "newname")
            ("bid",       xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
    }

    // Should be able to bidrefund
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "bidname"_n, user, mutable_variant_object()
                ("bidder",    user2)
                ("newname",   "newname")
                ("bid",       xyz("1.5000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user2")
        );

        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "bidname"_n, user2, mutable_variant_object()
            ("bidder",    user2)
            ("newname",   "newname")
            ("bid",       xyz("1.5000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance);

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "bidrefund"_n, user2, mutable_variant_object()
                ("bidder",    user)
                ("newname",   "newname")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "bidrefund"_n, user, mutable_variant_object()
            ("bidder",    user)
            ("newname",   "newname")
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance + xyz("1.0000"));
    }

    // Should be able to buyram
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "buyram"_n, user2, mutable_variant_object()
                ("payer",    user)
                ("receiver", user)
                ("quant", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

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
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "buyramself"_n, user2, mutable_variant_object()
                ("payer",    user)
                ("quant", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "buyramself"_n, user, mutable_variant_object()
            ("payer",    user)
            ("quant", xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));
    }

    // Should be able to buyramburn
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "buyramburn"_n, user2, mutable_variant_object()
                ("payer",    user)
                ("quantity", xyz("1.0000"))
                ("memo", std::string("memo"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

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
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "buyrambytes"_n, user2, mutable_variant_object()
                ("payer",    user)
                ("receiver", user)
                ("bytes", 1024)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

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
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "ramburn"_n, user2, mutable_variant_object()
                ("owner",    user)
                ("bytes", 10)
                ("memo", "memo")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "ramburn"_n, user, mutable_variant_object()
            ("owner",    user)
            ("bytes", 10)
            ("memo", "memo")
        );
    }

    // Should be able to sellram
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "sellram"_n, user2, mutable_variant_object()
                ("account",    user)
                ("bytes", 1024)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        auto old_balance = get_xyz_balance(user);
        auto old_balance_eos = get_balance(user);
        base_tester::push_action( xyz_name, "sellram"_n, user, mutable_variant_object()
            ("account",    user)
            ("bytes", 1024)
        );

        BOOST_REQUIRE_EQUAL(get_balance(user), old_balance_eos);
        BOOST_REQUIRE_EQUAL(get_xyz_balance(user) > old_balance, true);
    }

    // should be able to giftram
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "giftram"_n, user2, mutable_variant_object()
                ("from",    user)
                ("receiver",    user2)
                ("ram_bytes", 10)
                ("memo", "")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        auto bytes_from_before = get_ram_bytes(user);
        auto bytes_receiver_before = get_ram_bytes(user2);
        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "giftram"_n, user, mutable_variant_object()
            ("from",    user)
            ("receiver",    user2)
            ("ram_bytes", 10)
            ("memo", "")
        );

        auto bytes_from_after = get_ram_bytes(user);
        auto bytes_receiver_after = get_ram_bytes(user2);

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance);

        BOOST_REQUIRE_EQUAL(bytes_from_after, bytes_from_before - 10);
        BOOST_REQUIRE_EQUAL(bytes_receiver_after, bytes_receiver_before + 10);
    }

    // ungiftram
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "ungiftram"_n, user, mutable_variant_object()
                ("from",    user2)
                ("to",    user)
                ("memo", "")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user2")
        );

        auto bytes_from_before = get_ram_bytes(user);
        auto bytes_receiver_before = get_ram_bytes(user2);
        auto old_balance = get_xyz_balance(user);

        base_tester::push_action( xyz_name, "ungiftram"_n, user2, mutable_variant_object()
            ("from",    user2)
            ("to",    user)
            ("memo", "")
        );

        auto bytes_from_after = get_ram_bytes(user);
        auto bytes_receiver_after = get_ram_bytes(user2);

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance);

        BOOST_REQUIRE_EQUAL(bytes_from_after, bytes_from_before + 10);
        BOOST_REQUIRE_EQUAL(bytes_receiver_after, bytes_receiver_before - 10);

    }

    // should be able to stake to rex
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "deposit"_n, user2, mutable_variant_object()
                ("owner",    user)
                ("amount", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        auto old_balance = get_xyz_balance(user);
        base_tester::push_action( xyz_name, "deposit"_n, user, mutable_variant_object()
            ("owner",    user)
            ("amount", xyz("1.0000"))
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));

        auto rex_fund = get_rex_fund(user);
        BOOST_REQUIRE_EQUAL(rex_fund, eos("1.0000"));

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "buyrex"_n, user2, mutable_variant_object()
                ("from",    user)
                ("amount", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "buyrex"_n, user, mutable_variant_object()
            ("from",    user)
            ("amount", xyz("1.0000"))
        );

        auto rex_balance = get_rex_balance(user);
        BOOST_REQUIRE_EQUAL(rex_balance, rex(10000'0000));
    }

    // should be able to unstake from rex
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "mvfrsavings"_n, user2, mutable_variant_object()
                ("owner",    user)
                ("rex", rex(10000'0000))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

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

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "sellrex"_n, user2, mutable_variant_object()
                ("from",    user)
                ("rex", rex(10000'0000))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        // sell rex
        base_tester::push_action( xyz_name, "sellrex"_n, user, mutable_variant_object()
            ("from",    user)
            ("rex", rex(10000'0000))
        );
    }

    // should be able to withdraw
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "withdraw"_n, user2, mutable_variant_object()
                ("owner",    user)
                ("amount", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

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
        {
            base_tester::push_action( xyz_name, "deposit"_n, user, mutable_variant_object()
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

        base_tester::push_action( eos_name, "buyram"_n, eos_name, mutable_variant_object()
            ("payer",    eos_name)
            ("receiver", contract_account)
            ("quant", eos("1000000.0000"))
        );

        auto code = prepare_wasm(eos_contracts::fees_wasm());

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "setcode"_n, user, mutable_variant_object()
                ("account",    contract_account)
                ("vmtype",     0)
                ("vmversion",  0)
                ("code",       code )
                ("memo",       "")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of contractest")
        );

        base_tester::push_action( xyz_name, "setcode"_n, contract_account, mutable_variant_object()
            ("account",    contract_account)
            ("vmtype",     0)
            ("vmversion",  0)
            ("code",       code )
            ("memo",       "")
        );

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "setabi"_n, user, mutable_variant_object()
                ("account",    contract_account)
                ("abi",        eos_contracts::token_abi() )
                ("memo",       "")
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of contractest")
        );

        base_tester::push_action( xyz_name, "setabi"_n, contract_account, mutable_variant_object()
            ("account",    contract_account)
            ("abi",        eos_contracts::token_abi() )
            ("memo",       "")
        );

    }


    transfer(eos_name, user, eos("100000.0000"));
    transfer(user, xyz_name, eos("100000.0000"), user);
    vector<name> producers = active_and_vote_producers();


    // should be able to delegate and undelegate bw
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "delegatebw"_n, user2, mutable_variant_object()
                ("from",    user)
                ("receiver", user)
                ("stake_net_quantity", xyz("1.0000"))
                ("stake_cpu_quantity", xyz("100000.0000"))
                ("transfer", false)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        auto old_balance = get_xyz_balance(user) - xyz("100000.0000");
        base_tester::push_action( xyz_name, "delegatebw"_n, user, mutable_variant_object()
            ("from",    user)
            ("receiver", user)
            ("stake_net_quantity", xyz("1.0000"))
            ("stake_cpu_quantity", xyz("100000.0000"))
            ("transfer", false)
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(user), old_balance - xyz("1.0000"));

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "undelegatebw"_n, user2, mutable_variant_object()
                ("from",    user)
                ("receiver", user)
                ("unstake_net_quantity", xyz("0.0000"))
                ("unstake_cpu_quantity", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

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

    // should be able to unstaketorex
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "unstaketorex"_n, user2, mutable_variant_object()
                ("owner",    user)
                ("receiver", user)
                ("from_net", xyz("0.0000"))
                ("from_cpu", xyz("1.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "unstaketorex"_n, user, mutable_variant_object()
            ("owner",    user)
            ("receiver", user)
            ("from_net", xyz("0.0000"))
            ("from_cpu", xyz("1.0000"))
        );
    }

    // claimrewards
    {
        auto producer = producers[0];
        auto old_balance = get_xyz_balance(producer);
        base_tester::push_action( xyz_name, "claimrewards"_n, producer, mutable_variant_object()
            ("owner",    producer)
        );

        BOOST_REQUIRE_EQUAL(get_xyz_balance(producer) > old_balance, true);

        // should not be able to claimrewards for another account
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "claimrewards"_n, user, mutable_variant_object()
                ("owner",    producer)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of defproducera")
        );
    }

    // linkauth
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "linkauth"_n, user2, mutable_variant_object()
                ("account",    user)
                ("code",       xyz_name)
                ("type",       "transfer"_n)
                ("requirement", "active"_n)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "linkauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("code",       xyz_name)
            ("type",       "transfer"_n)
            ("requirement", "active"_n)
        );
    }

    // unlinkauth
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "unlinkauth"_n, user2, mutable_variant_object()
                ("account",    user)
                ("code",       xyz_name)
                ("type",       "transfer"_n)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "unlinkauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("code",       xyz_name)
            ("type",       "transfer"_n)
        );
    }

    // updateauth and deleteauth
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "updateauth"_n, user2, mutable_variant_object()
                ("account",    user)
                ("permission", "test"_n)
                ("parent",     "active"_n)
                ("auth",       authority(1, {key_weight{get_public_key(user, "active"), 1}}))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "updateauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("permission", "test"_n)
            ("parent",     "active"_n)
            ("auth",       authority(1, {key_weight{get_public_key(user, "active"), 1}}))
        );

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "deleteauth"_n, user2, mutable_variant_object()
                ("account",    user)
                ("permission", "test"_n)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "deleteauth"_n, user, mutable_variant_object()
            ("account",    user)
            ("permission", "test"_n)
        );
    }

    // voteproducer
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "voteproducer"_n, user2, mutable_variant_object()
                ("voter",    user)
                ("proxy",    ""_n)
                ("producers", std::vector<name>{producers[0]})
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "voteproducer"_n, user, mutable_variant_object()
            ("voter",    user)
            ("proxy",    ""_n)
            ("producers", std::vector<name>{producers[0]})
        );
    }

    // voteupdate
    {
        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "voteupdate"_n, user2, mutable_variant_object()
                ("voter_name",    user)
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of user")
        );

        base_tester::push_action( xyz_name, "voteupdate"_n, user, mutable_variant_object()
            ("voter_name",    user)
        );
    }


    // should be able to powerup and get overages back in XYZ
    {
        // configure powerup
        {
            powerup_config config;

            config.net.current_weight_ratio = powerup_frac / 4;
            config.net.target_weight_ratio  = powerup_frac / 100;
            config.net.assumed_stake_weight = stake_weight;
            config.net.target_timestamp     = time_point_sec(get_pending_block_time() + fc::days(100));
            config.net.exponent             = 2;
            config.net.decay_secs           = fc::days(1).to_seconds();
            config.net.min_price            = asset::from_string("0.0000 EOS");
            config.net.max_price            = asset::from_string("1000000.0000 EOS");

            config.cpu.current_weight_ratio = powerup_frac / 4;
            config.cpu.target_weight_ratio  = powerup_frac / 100;
            config.cpu.assumed_stake_weight = stake_weight;
            config.cpu.target_timestamp     = time_point_sec(get_pending_block_time() + fc::days(100));
            config.cpu.exponent             = 2;
            config.cpu.decay_secs           = fc::days(1).to_seconds();
            config.cpu.min_price            = asset::from_string("0.0000 EOS");
            config.cpu.max_price            = asset::from_string("1000000.0000 EOS");

            config.powerup_days    = 30;
            config.min_powerup_fee = asset::from_string("1.0000 EOS");

            base_tester::push_action(eos_name, "cfgpowerup"_n, eos_name, mvo()("args", config));
        }

        auto old_balance = get_xyz_balance(powerupuser);

        BOOST_REQUIRE_EXCEPTION(
            base_tester::push_action( xyz_name, "powerup"_n, user, mutable_variant_object()
                ("payer",    powerupuser)
                ("receiver", powerupuser)
                ("days", 30)
                ("net_frac", powerup_frac/4)
                ("cpu_frac", powerup_frac/4)
                ("max_payment", xyz("100000.0000"))
            ),
            missing_auth_exception,
            fc_exception_message_is("missing authority of powuser")
        );

        // 62500.0000 EOS is fee
        base_tester::push_action( xyz_name, "powerup"_n, powerupuser, mutable_variant_object()
            ("payer",    powerupuser)
            ("receiver", powerupuser)
            ("days", 30)
            ("net_frac", powerup_frac/4)
            ("cpu_frac", powerup_frac/4)
            ("max_payment", xyz("100000.0000"))
        );

        // new balance should be old balance - 62500.0000 EOS
        BOOST_REQUIRE_EQUAL(get_xyz_balance(powerupuser), old_balance - xyz("62500.0000"));
    }


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()