#pragma once

#include "contracts.hpp"
#include "test_symbol.hpp"
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/variant_object.hpp>
#include <fstream>
#include <ranges>


using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

namespace eosio_system {

class eosio_system_tester : public validating_tester {
public:
   // -----------------
   // static utilities
   // -----------------
   static constexpr account_name xyz_name = "xyz"_n;
   static constexpr account_name eos_name = "eosio"_n;

   static asset eos(const char* amount) { return core_sym::from_string(amount); }
   static asset xyz(const char* amount) { return xyz_core_sym::from_string(amount); }
   static asset rex(uint64_t amount)    { return asset(amount, symbol(SY(4, REX))); }

   static symbol xyz_symbol() { return symbol{XYZ_SYM}; }
   static symbol eos_symbol() { return symbol{CORE_SYM}; }

   ~eosio_system_tester() {
      skip_validate = true;
   }

   // -----------------
   // contract
   // -----------------
   struct contract {
      contract(account_name name, eosio_system_tester& tester)
         : _contract_name(name)
         , _tester(tester) {}

      static bytes serialize(abi_serializer& ser, action_name act, const variant_object& data) {
         string action_type_name = ser.get_action_type(act);
         return ser.variant_to_binary(action_type_name, data, abi_serializer_max_time);
      }

      template <typename... Args>
      auto push_action_to_base(Args&&... args) {
         return static_cast<base_tester&>(_tester).push_action(std::forward<Args>(args)...);
      }

      action_result push_action(account_name signer, action_name act, bytes params, vector<permission_level> auths) {
         signed_transaction trx;
         trx.actions.emplace_back(action(auths, _contract_name, act, std::move(params)));
         _tester.set_transaction_headers(trx);

         for (auto auth : auths)
            trx.sign(get_private_key(auth.actor, auth.permission.to_string()), _tester.control->get_chain_id());

         try {
            _tester.push_transaction(trx);
         } catch (const fc::exception& ex) {
            edump((ex.to_detail_string()));
            auto msg = ex.top_message(); // top_message() is assumed by many tests; otherwise they fail
            if (msg.starts_with("assertion failure with message: "))
               msg = msg.substr(strlen("assertion failure with message: "));
            return error(msg);
         }
         _tester.produce_block();
         BOOST_REQUIRE_EQUAL(true, _tester.chain_has_transaction(trx.id()));
         return success();
      }

      action_result push_action(account_name signer, action_name act, bytes params, vector<name> actors) {
         vector<permission_level> auths;
         for (auto n : actors)
            auths.emplace_back(n, config::active_name);
         return push_action(signer, act, std::move(params), std::move(auths));
      }

      // -----------------
      // supported actions
      // -----------------
      action_result transfer(name from, name to, const asset& amount) { // both xyz and system contracts
         auto act = "transfer"_n;
         auto params =
            serialize(_tester.token_abi_ser, act, mvo()("from", from)("to", to)("quantity", amount)("memo", ""));
         return push_action(from, act, std::move(params), {from});
      }

      action_result swapto(name from, name to, const asset& amount) { // this action available only on xyz contract
         auto act = "swapto"_n;
         auto params =
            serialize(_tester.xyz_abi_ser, act, mvo()("from", from)("to", to)("quantity", amount)("memo", ""));
         return push_action(_contract_name, act, std::move(params), {from});
      }

      action_result bidname(name bidder, name newname, const asset& bid) {
         auto act    = "bidname"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("bidder", bidder)("newname", newname)("bid", bid));
         return push_action(_contract_name, act, std::move(params), {bidder});
      }

      action_result bidrefund(name bidder, name newname) {
         auto act    = "bidrefund"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("bidder", bidder)("newname", newname));
         return push_action(_contract_name, act, std::move(params), {bidder});
      }

      action_result buyram(name payer, name receiver, const asset& quant) {
         auto act    = "buyram"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("payer", payer)("receiver", receiver)("quant", quant));
         return push_action(_contract_name, act, std::move(params), {payer});
      }

      action_result buyramburn(name payer, const asset& quantity) {
         auto act    = "buyramburn"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("payer", payer)("quantity", quantity)("memo", ""));
         return push_action(_contract_name, act, std::move(params), {payer});
      }

      action_result buyrambytes(name payer, name receiver, uint32_t bytes) {
         auto act    = "buyrambytes"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("payer", payer)("receiver", receiver)("bytes", bytes));
         return push_action(_contract_name, act, std::move(params), {payer});
      }

      action_result buyramself(name payer, const asset& quant) {
         auto act    = "buyramself"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("payer", payer)("quant", quant));
         return push_action(_contract_name, act, std::move(params), {payer});
      }

      action_result ramburn(name owner, int64_t bytes) {
         auto act    = "ramburn"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("owner", owner)("bytes", bytes)("memo", ""));
         return push_action(_contract_name, act, std::move(params), {owner});
      }

      action_result ramtransfer(name from, name to, int64_t bytes) {
         auto act    = "ramtransfer"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("from", from)("to", to)("bytes", bytes)("memo", ""));
         return push_action(_contract_name, act, std::move(params), {from});
      }

      action_result sellram(name account, int64_t bytes) {
         auto act    = "sellram"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("account", account)("bytes", bytes));
         return push_action(_contract_name, act, std::move(params), {account});
      }

      action_result deposit(name owner, const asset& amount) {
         auto act    = "deposit"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("owner", owner)("amount", amount));
         return push_action(_contract_name, act, std::move(params), {owner});
      }

      action_result buyrex(name from, const asset& amount) {
         auto act    = "buyrex"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("from", from)("amount", amount));
         return push_action(_contract_name, act, std::move(params), {from});
      }

      action_result sellrex(name from, const asset& rex) {
         auto act    = "sellrex"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("from", from)("rex", rex));
         return push_action(_contract_name, act, std::move(params), {from});
      }

      action_result mvtosavings(name owner, const asset& rex) {
         auto act    = "mvtosavings"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("owner", owner)("rex", rex));
         return push_action(_contract_name, act, std::move(params), {owner});
      }

      action_result mvfrsavings(name owner, const asset& rex) {
         auto act    = "mvfrsavings"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("owner", owner)("rex", rex));
         return push_action(_contract_name, act, std::move(params), {owner});
      }

      action_result withdraw(name owner, const asset& amount) {
         auto act    = "withdraw"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("owner", owner)("amount", amount));
         return push_action(_contract_name, act, std::move(params), {owner});
      }

      action_result delegatebw(name from, name receiver, const asset& stake_net_quantity,
                               const asset& stake_cpu_quantity, bool transfer) {
         auto act    = "delegatebw"_n;
         auto params = serialize(_tester.xyz_abi_ser, act,
                                 mvo()("from", from)("receiver", receiver)("stake_net_quantity", stake_net_quantity)(
                                    "stake_cpu_quantity", stake_cpu_quantity)("transfer", transfer));
         return push_action(_contract_name, act, std::move(params), {from});
      }

      action_result undelegatebw(name from, name receiver, const asset& unstake_net_quantity,
                                 const asset& unstake_cpu_quantity) {
         auto act    = "undelegatebw"_n;
         auto params = serialize(_tester.xyz_abi_ser, act,
                                 mvo()("from", from)("receiver", receiver)("unstake_net_quantity", unstake_net_quantity)(
                                    "unstake_cpu_quantity", unstake_cpu_quantity));
         return push_action(_contract_name, act, std::move(params), {from});
      }

      action_result refund(name owner) {
         auto act    = "refund"_n;
         auto params = serialize(_tester.xyz_abi_ser, act, mvo()("owner", owner));
         return push_action(_contract_name, act, std::move(params), {owner});
      }

      account_name         _contract_name;
      eosio_system_tester& _tester;
   };

   // --------------------
   // check token balances
   // --------------------
   int8_t get_xyz_account_released(account_name account) const {
      vector<char> data = get_row_by_account(xyz_name, account, "accounts"_n, account_name(xyz_symbol().to_symbol_code().value));
      if (data.empty())
         return -1;
      return xyz_abi_ser.binary_to_variant("account", data, abi_serializer_max_time)["released"].as<int8_t>();
   }

   asset get_balance(name code, account_name act, symbol token) const {
      vector<char> data = get_row_by_account(code, act, "accounts"_n, account_name(token.to_symbol_code().value));
      if (data.empty())
         return asset(0, token);
      return token_abi_ser.binary_to_variant("account", data, abi_serializer_max_time)["balance"].as<asset>();
   }

   asset get_eos_balance(account_name act) const { return get_balance("eosio.token"_n, act, eos_symbol()); }

   asset get_xyz_balance(account_name act) const { return get_balance(xyz_name, act, xyz_symbol()); }

   bool check_balances(account_name act, const vector<asset>& assets) const {
      for (const auto& a : assets) {
         if (a.get_symbol() == xyz_symbol()) {
            if (get_xyz_balance(act) != a)
               return false;
         } else if (a.get_symbol() == eos_symbol()) {
            if (get_eos_balance(act) != a)
               return false;
         } else {
            return false;
         }
      }
      return true;
   }

   // -----------------
   // check ram balance
   // -----------------
   fc::variant get_total_stake(account_name act) const {
      vector<char> data = get_row_by_account(eos_name, act, "userres"_n, act);
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant("user_resources", data, abi_serializer_max_time);
   }

   int64_t get_ram_bytes(account_name act) const { return get_total_stake(act)["ram_bytes"].as_int64(); }

   // -----------------
   // members
   // -----------------
   contract eosio_token;
   contract eosio_xyz;
   contract eosio;

   void set_code_and_abi(account_name account, const vector<uint8_t>& wasm, const std::string& abi_json,
                         const private_key_type* signer = nullptr) try {
      set_code(account, wasm, signer);
      set_abi(account, abi_json, signer);
   }
   FC_CAPTURE_AND_RETHROW((account))

   void create_serializer(account_name account, abi_serializer& ser) {
      const auto& accnt = control->db().get<account_object, by_name>(account);
      abi_def     abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      ser.set_abi(abi, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   eosio_system_tester()
      : validating_tester({}, nullptr, setup_policy::full)
      , eosio_token("eosio.token"_n, *this)
      , eosio_xyz(xyz_name, *this)
      , eosio("eosio"_n, *this) {

      // -------- create accounts -----------------------------------------------------------------
      produce_block();
      create_accounts({"eosio.token"_n, "eosio.ram"_n, "eosio.ramfee"_n, "eosio.stake"_n, "eosio.bpay"_n,
                       "eosio.vpay"_n, "eosio.saving"_n, "eosio.names"_n, "eosio.rex"_n, "eosio.fees"_n, xyz_name});
      produce_blocks(5);

      // -------- eosio.token contract -----------------------------------------------------------------
      set_code_and_abi("eosio.token"_n, eos_contracts::token_wasm(), eos_contracts::token_abi().data());
      create_serializer("eosio.token"_n, token_abi_ser);

      // -------- eosio.fees contract -----------------------------------------------------------------
      set_code("eosio.fees"_n, eos_contracts::fees_wasm());

      // -------- eosio.bpay contract
      // --------------------------------------------------------------------------------------------
      set_code_and_abi("eosio.bpay"_n, eos_contracts::bpay_wasm(), eos_contracts::bpay_abi().data());
      create_serializer("eosio.bpay"_n, bpay_abi_ser);

      // -------- create core tokens ------------------------------------------------------------------
      symbol core_symbol = symbol{CORE_SYM};
      FC_ASSERT(core_symbol.decimals() == 4, "create_core_token assumes core token has 4 digits of precision");
      create_currency("eosio.token"_n, config::system_account_name, asset(100000000000000, core_symbol));
      issue(asset(10000000000000, core_symbol));
      BOOST_REQUIRE_EQUAL(asset(10000000000000, core_symbol), get_balance("eosio", core_symbol));


      // -------- eosio contract ------------------------------------------------------------------
      set_code_and_abi(config::system_account_name, eos_contracts::system_wasm(), eos_contracts::system_abi().data());
      create_serializer(config::system_account_name, abi_ser);

      base_tester::push_action(config::system_account_name, "init"_n,  // call `init` on system contract
                               config::system_account_name,
                               mutable_variant_object()("version", 0)("core", CORE_SYM_STR));

      // -------- xyz contract --------------------------------------------------------------------
      set_code_and_abi(xyz_name, xyz_contracts::system_wasm(), xyz_contracts::system_abi().data());
      create_serializer(xyz_name, xyz_abi_ser);

      base_tester::push_action(xyz_name, "init"_n,                      // call `init` on xyz contract
                               {config::system_account_name, xyz_name},
                               mutable_variant_object()("maximum_supply", xyz("2100000000.0000")));

      base_tester::push_action(config::system_account_name, "setpriv"_n, // provide `priv` permission
                               config::system_account_name,
                               mutable_variant_object()("account", xyz_name)("is_priv", 1));

      // -------- Assumes previous setup steps were done with core token symbol set to CORE_SYM
      create_account_with_resources( "alice1111111"_n, config::system_account_name, core_sym::from_string("1.0000"), false );
      create_account_with_resources( "bob111111111"_n, config::system_account_name, core_sym::from_string("0.4500"), false );
   }

   void create_accounts_with_resources(vector<account_name> accounts,
                                       account_name         creator = config::system_account_name) {
      for (auto a : accounts)
         create_account_with_resources(a, creator);
   }

   transaction_trace_ptr create_account_with_resources(account_name a, account_name creator, uint32_t ram_bytes = 8000,
                                                       uint32_t gifted_ram_bytes = 0) {
      signed_transaction trx;
      set_transaction_headers(trx);

      authority owner_auth;
      owner_auth = authority(get_public_key(a, "owner"));
      auto perms = vector<permission_level>{ {creator, config::active_name} };

      trx.actions.emplace_back(perms, newaccount{.creator = creator,
                                                 .name    = a,
                                                 .owner   = owner_auth,
                                                 .active  = authority(get_public_key(a, "active"))});

      if (ram_bytes) {
         trx.actions.emplace_back(get_action(config::system_account_name, "buyrambytes"_n, perms,
                                             mvo()("payer", creator)("receiver", a)("bytes", ram_bytes)));
      }

      if (gifted_ram_bytes) {
         trx.actions.emplace_back(get_action(config::system_account_name, "giftram"_n, perms,
                                             mvo()("from", creator)("to", a)("bytes", gifted_ram_bytes)(
                                                "memo", "Initial RAM gift at account creation")));
      }

      trx.actions.emplace_back(get_action(config::system_account_name, "delegatebw"_n, perms,
                                          mvo()("from", creator)("receiver", a)("stake_net_quantity", eos("10.0000"))(
                                             "stake_cpu_quantity", eos("10.0000"))("transfer", 0)));

      set_transaction_headers(trx);
      trx.sign(get_private_key(creator, "active"), control->get_chain_id());
      return push_transaction(trx);
   }

   transaction_trace_ptr create_account_with_resources(account_name a, account_name creator, asset ramfunds,
                                                       bool multisig, asset net = eos("10.0000"),
                                                       asset cpu = eos("10.0000")) {
      signed_transaction trx;
      set_transaction_headers(trx);

      authority owner_auth;
      if (multisig) {
         // multisig between account's owner key and creators active permission
         owner_auth = authority(2,
                                {
                                   key_weight{get_public_key(a, "owner"), 1}
         },
                                {permission_level_weight{{creator, config::active_name}, 1}});
      } else {
         owner_auth = authority(get_public_key(a, "owner"));
      }

      trx.actions.emplace_back(
         vector<permission_level>{
            {creator, config::active_name}
      },
         newaccount{
            .creator = creator, .name = a, .owner = owner_auth, .active = authority(get_public_key(a, "active"))});

      trx.actions.emplace_back(get_action(config::system_account_name, "buyram"_n,
                                          vector<permission_level>{
                                             {creator, config::active_name}
      },
                                          mvo()("payer", creator)("receiver", a)("quant", ramfunds)));

      trx.actions.emplace_back(get_action(
         config::system_account_name, "delegatebw"_n,
         vector<permission_level>{
            {creator, config::active_name}
      },
         mvo()("from", creator)("receiver", a)("stake_net_quantity", net)("stake_cpu_quantity", cpu)("transfer", 0)));

      set_transaction_headers(trx);
      trx.sign(get_private_key(creator, "active"), control->get_chain_id());
      return push_transaction(trx);
   }

   /*
    * Doing an idump((trace->action_traces[i].return_value)) will show the full hex
    * Converting return_value to a string will transform it into a series of ordianl values represented by chars
    * fc::to_hex() did not work.
    * Couldn't find a method to convert so wrote my own.
    */
   std::string convert_ordinals_to_hex(const std::string& ordinals) {
      // helper to convert to hex, 2 chars for hex, 3 char null terminator
      char hex_temp[3];
      // return string
      std::string hex_as_chars;

      for (unsigned char c : ordinals) {
         // convert to hex from ordinal
         sprintf(hex_temp, "%x", (int)c);
         if (hex_temp[1] == '\0') {
            hex_temp[1] = hex_temp[0];
            hex_temp[0] = '0';
            // null terminate
            hex_temp[2] = '\0';
         }
         hex_as_chars += hex_temp[0];
         hex_as_chars += hex_temp[1];
      }
      return hex_as_chars;
   }

   std::string convert_json_to_hex(const type_name& type, const std::string& json) {
      // ABI for our return struct
      const char* ramtransfer_return_abi = R"=====(
   {
      "version": "eosio::abi/1.2",
      "types": [],
      "structs": [
         {
             "name": "action_return_buyram",
             "base": "",
             "fields": [
                 {
                     "name": "payer",
                     "type": "name"
                 },
                 {
                     "name": "receiver",
                     "type": "name"
                 },
                 {
                     "name": "quantity",
                     "type": "asset"
                 },
                 {
                     "name": "bytes_purchased",
                     "type": "int64"
                 },
                 {
                     "name": "ram_bytes",
                     "type": "int64"
                 },
                 {
                      "name": "fee",
                      "type": "asset"
                  }
             ]
         },
         {
            "name": "action_return_ramtransfer",
            "base": "",
            "fields": [
            {
               "name": "from",
               "type": "name"
            },
            {
               "name": "to",
               "type": "name"
            },
            {
               "name": "bytes",
               "type": "int64"
            },
            {
               "name": "from_ram_bytes",
               "type": "int64"
            },
            {
               "name": "to_ram_bytes",
               "type": "int64"
            }
            ]
         },
         {
             "name": "action_return_sellram",
             "base": "",
             "fields": [
                 {
                     "name": "account",
                     "type": "name"
                 },
                 {
                     "name": "quantity",
                     "type": "asset"
                 },
                 {
                     "name": "bytes_sold",
                     "type": "int64"
                 },
                 {
                     "name": "ram_bytes",
                     "type": "int64"
                 },
                 {
                      "name": "fee",
                      "type": "asset"
                  }
             ]
         },
      ],
      "actions": [],
      "tables": [],
      "ricardian_clauses": [],
      "variants": [],
      "action_results": [
            {
                "name": "buyram",
                "result_type": "action_return_buyram"
            },
            {
                "name": "buyrambytes",
                "result_type": "action_return_buyram"
            },
            {
                "name": "buyramself",
                "result_type": "action_return_buyram"
            },
            {
                "name": "ramburn",
                "result_type": "action_return_ramtransfer"
            },
            {
                "name": "ramtransfer",
                "result_type": "action_return_ramtransfer"
            },
            {
                "name": "sellram",
                "result_type": "action_return_sellram"
            }
      ]
   }
   )=====";

      // create abi to parse return values
      auto           abi = fc::json::from_string(ramtransfer_return_abi).as<abi_def>();
      abi_serializer ramtransfer_return_serializer =
         abi_serializer{std::move(abi), abi_serializer::create_yield_function(abi_serializer_max_time)};

      auto return_json      = fc::json::from_string(json);
      auto serialized_bytes = ramtransfer_return_serializer.variant_to_binary(
         type, return_json, abi_serializer::create_yield_function(abi_serializer_max_time));
      return fc::to_hex(serialized_bytes);
   }

   template <class Action>
   void validate_action_return(Action&& act, const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      // execute transaction and get traces must use base_tester
      transaction_trace_ptr trace = std::forward<Action>(act)();

      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      /*
       * This is assignment is giving me grief
       * Doing an idump((trace->action_traces[i].return_value)) will show the full hex
       * Inspecting trace->action_traces[i].return_value shows the hex have been converted to ordinals
       * Couldn't find a method to convert so wrote my own.
       */
      // the first trace always has the return value
      int         i = 0;
      std::string copy_trace =
         std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex, actual_hex);
   }

   transaction_trace_ptr setup_producer_accounts(const std::vector<account_name>& accounts,
                                                 asset                            ram = eos("1.0000"),
                                                 asset cpu = eos("80.0000"),
                                                 asset net = eos("80.0000")) {
      account_name       creator(config::system_account_name);
      signed_transaction trx;
      set_transaction_headers(trx);

      for (const auto& a : accounts) {
         authority owner_auth(get_public_key(a, "owner"));
         trx.actions.emplace_back(
            vector<permission_level>{
               {creator, config::active_name}
         },
            newaccount{
               .creator = creator, .name = a, .owner = owner_auth, .active = authority(get_public_key(a, "active"))});

         trx.actions.emplace_back(get_action(config::system_account_name, "buyram"_n,
                                             vector<permission_level>{
                                                {creator, config::active_name}
         },
                                             mvo()("payer", creator)("receiver", a)("quant", ram)));

         trx.actions.emplace_back(get_action(config::system_account_name, "delegatebw"_n,
                                             vector<permission_level>{
                                                {creator, config::active_name}
         },
                                             mvo()("from", creator)("receiver", a)("stake_net_quantity", net)(
                                                "stake_cpu_quantity", cpu)("transfer", 0)));
      }

      set_transaction_headers(trx);
      trx.sign(get_private_key(creator, "active"), control->get_chain_id());
      return push_transaction(trx);
   }

   action_result push_action(const account_name& signer, const action_name& name, const variant_object& data) {
      string action_type_name = abi_ser.get_action_type(name);

      action act;
      act.account = config::system_account_name;
      act.name    = name;
      act.data    = abi_ser.variant_to_binary(action_type_name, data, abi_serializer_max_time);

      return base_tester::push_action(std::move(act), signer.to_uint64_t());
   }

   action_result stake(const account_name& from, const account_name& to, const asset& net, const asset& cpu) {
      return push_action(
         name(from), "delegatebw"_n,
         mvo()("from", from)("receiver", to)("stake_net_quantity", net)("stake_cpu_quantity", cpu)("transfer", 0));
   }
   action_result stake(std::string_view from, std::string_view to, const asset& net, const asset& cpu) {
      return stake(account_name(from), account_name(to), net, cpu);
   }

   action_result stake(const account_name& acnt, const asset& net, const asset& cpu) {
      return stake(acnt, acnt, net, cpu);
   }
   action_result stake(std::string_view acnt, const asset& net, const asset& cpu) {
      return stake(account_name(acnt), net, cpu);
   }

   action_result stake_with_transfer(const account_name& from, const account_name& to, const asset& net,
                                     const asset& cpu) {
      return push_action(
         name(from), "delegatebw"_n,
         mvo()("from", from)("receiver", to)("stake_net_quantity", net)("stake_cpu_quantity", cpu)("transfer", true));
   }
   action_result stake_with_transfer(std::string_view from, std::string_view to, const asset& net, const asset& cpu) {
      return stake_with_transfer(account_name(from), account_name(to), net, cpu);
   }

   action_result stake_with_transfer(const account_name& acnt, const asset& net, const asset& cpu) {
      return stake_with_transfer(acnt, acnt, net, cpu);
   }
   action_result stake_with_transfer(std::string_view acnt, const asset& net, const asset& cpu) {
      return stake_with_transfer(account_name(acnt), net, cpu);
   }

   action_result unstake(const account_name& from, const account_name& to, const asset& net, const asset& cpu) {
      return push_action(name(from), "undelegatebw"_n,
                         mvo()("from", from)("receiver", to)("unstake_net_quantity", net)("unstake_cpu_quantity", cpu));
   }
   action_result unvest(const account_name& account, const asset& net, const asset& cpu) {
      return push_action("eosio"_n, "unvest"_n,
                         mvo()("account", account)("unvest_net_quantity", net)("unvest_cpu_quantity", cpu));
   }
   action_result unstake(std::string_view from, std::string_view to, const asset& net, const asset& cpu) {
      return unstake(account_name(from), account_name(to), net, cpu);
   }

   action_result unstake(const account_name& acnt, const asset& net, const asset& cpu) {
      return unstake(acnt, acnt, net, cpu);
   }
   action_result unstake(std::string_view acnt, const asset& net, const asset& cpu) {
      return unstake(account_name(acnt), net, cpu);
   }

   int64_t bancor_convert(int64_t S, int64_t R, int64_t T) { return double(R) * T / (double(S) + T); };

   int64_t get_net_limit(account_name a) {
      int64_t ram_bytes = 0, net = 0, cpu = 0;
      control->get_resource_limits_manager().get_account_limits(a, ram_bytes, net, cpu);
      return net;
   };

   int64_t get_cpu_limit(account_name a) {
      int64_t ram_bytes = 0, net = 0, cpu = 0;
      control->get_resource_limits_manager().get_account_limits(a, ram_bytes, net, cpu);
      return cpu;
   };

   auto get_account_ram(const account_name& a) {
      int64_t ram_bytes = 0, net = 0, cpu = 0;
      auto rlm = control->get_resource_limits_manager();
      auto ram_usage = rlm.get_account_ram_usage(a);
      control->get_resource_limits_manager().get_account_limits(a, ram_bytes, net, cpu);
      return ram_bytes - ram_usage;
   }

   action_result deposit(const account_name& owner, const asset& amount) {
      return push_action(name(owner), "deposit"_n, mvo()("owner", owner)("amount", amount));
   }

   action_result withdraw(const account_name& owner, const asset& amount) {
      return push_action(name(owner), "withdraw"_n, mvo()("owner", owner)("amount", amount));
   }

   action_result buyrex(const account_name& from, const asset& amount) {
      return push_action(name(from), "buyrex"_n, mvo()("from", from)("amount", amount));
   }

   asset get_buyrex_result(const account_name& from, const asset& amount) {
      auto trace =
         base_tester::push_action(config::system_account_name, "buyrex"_n, from, mvo()("from", from)("amount", amount));
      asset rex_received;
      for (size_t i = 0; i < trace->action_traces.size(); ++i) {
         if (trace->action_traces[i].act.name == "buyresult"_n) {
            fc::raw::unpack(trace->action_traces[i].act.data.data(), trace->action_traces[i].act.data.size(),
                            rex_received);
            return rex_received;
         }
      }
      return rex_received;
   }

   action_result unstaketorex(const account_name& owner, const account_name& receiver, const asset& from_net,
                              const asset& from_cpu) {
      return push_action(name(owner), "unstaketorex"_n,
                         mvo()("owner", owner)("receiver", receiver)("from_net", from_net)("from_cpu", from_cpu));
   }

   asset get_unstaketorex_result(const account_name& owner, const account_name& receiver, const asset& from_net,
                                 const asset& from_cpu) {
      auto trace = base_tester::push_action(
         config::system_account_name, "unstaketorex"_n, owner,
         mvo()("owner", owner)("receiver", receiver)("from_net", from_net)("from_cpu", from_cpu));
      asset rex_received;
      for (size_t i = 0; i < trace->action_traces.size(); ++i) {
         if (trace->action_traces[i].act.name == "buyresult"_n) {
            fc::raw::unpack(trace->action_traces[i].act.data.data(), trace->action_traces[i].act.data.size(),
                            rex_received);
            return rex_received;
         }
      }
      return rex_received;
   }

   action_result sellrex(const account_name& from, const asset& rex) {
      return push_action(name(from), "sellrex"_n, mvo()("from", from)("rex", rex));
   }

   asset get_sellrex_result(const account_name& from, const asset& rex) {
      auto trace =
         base_tester::push_action(config::system_account_name, "sellrex"_n, from, mvo()("from", from)("rex", rex));
      asset proceeds = eos("0.0000");
      for (size_t i = 0; i < trace->action_traces.size(); ++i) {
         if (trace->action_traces[i].act.name == "sellresult"_n) {
            asset _action_proceeds;
            fc::raw::unpack(trace->action_traces[i].act.data.data(), trace->action_traces[i].act.data.size(),
                            _action_proceeds);
            proceeds += _action_proceeds;
         }
      }
      return proceeds;
   }

   auto get_rexorder_result(const transaction_trace_ptr& trace) {
      std::vector<std::pair<account_name, asset>> output;
      for (size_t i = 0; i < trace->action_traces.size(); ++i) {
         if (trace->action_traces[i].act.name == "orderresult"_n) {
            fc::datastream<const char*> ds(trace->action_traces[i].act.data.data(),
                                           trace->action_traces[i].act.data.size());
            account_name                owner;
            fc::raw::unpack(ds, owner);
            asset proceeds;
            fc::raw::unpack(ds, proceeds);
            output.emplace_back(owner, proceeds);
         }
      }
      return output;
   }

   action_result cancelrexorder(const account_name& owner) {
      return push_action(name(owner), "cnclrexorder"_n, mvo()("owner", owner));
   }

   action_result rentcpu(const account_name& from, const account_name& receiver, const asset& payment,
                         const asset& fund = eos("0.0000")) {
      return push_action(name(from), "rentcpu"_n,
                         mvo()("from", from)("receiver", receiver)("loan_payment", payment)("loan_fund", fund));
   }

   action_result rentnet(const account_name& from, const account_name& receiver, const asset& payment,
                         const asset& fund = eos("0.0000")) {
      return push_action(name(from), "rentnet"_n,
                         mvo()("from", from)("receiver", receiver)("loan_payment", payment)("loan_fund", fund));
   }

   asset _get_rentrex_result(const account_name& from, const account_name& receiver, const asset& payment, bool cpu) {
      const name act   = cpu ? "rentcpu"_n : "rentnet"_n;
      auto       trace = base_tester::push_action(config::system_account_name, act, from,
                                                  mvo()("from", from)("receiver", receiver)("loan_payment", payment)(
                                               "loan_fund", eos("0.0000")));

      asset rented_tokens = eos("0.0000");
      for (size_t i = 0; i < trace->action_traces.size(); ++i) {
         if (trace->action_traces[i].act.name == "rentresult"_n) {
            fc::raw::unpack(trace->action_traces[i].act.data.data(), trace->action_traces[i].act.data.size(),
                            rented_tokens);
            return rented_tokens;
         }
      }
      return rented_tokens;
   }

   asset get_rentcpu_result(const account_name& from, const account_name& receiver, const asset& payment) {
      return _get_rentrex_result(from, receiver, payment, true);
   }

   asset get_rentnet_result(const account_name& from, const account_name& receiver, const asset& payment) {
      return _get_rentrex_result(from, receiver, payment, false);
   }

   action_result fundcpuloan(const account_name& from, const uint64_t loan_num, const asset& payment) {
      return push_action(name(from), "fundcpuloan"_n, mvo()("from", from)("loan_num", loan_num)("payment", payment));
   }

   action_result fundnetloan(const account_name& from, const uint64_t loan_num, const asset& payment) {
      return push_action(name(from), "fundnetloan"_n, mvo()("from", from)("loan_num", loan_num)("payment", payment));
   }


   action_result defundcpuloan(const account_name& from, const uint64_t loan_num, const asset& amount) {
      return push_action(name(from), "defcpuloan"_n, mvo()("from", from)("loan_num", loan_num)("amount", amount));
   }

   action_result defundnetloan(const account_name& from, const uint64_t loan_num, const asset& amount) {
      return push_action(name(from), "defnetloan"_n, mvo()("from", from)("loan_num", loan_num)("amount", amount));
   }

   action_result updaterex(const account_name& owner) {
      return push_action(name(owner), "updaterex"_n, mvo()("owner", owner));
   }

   action_result rexexec(const account_name& user, uint16_t max) {
      return push_action(name(user), "rexexec"_n, mvo()("user", user)("max", max));
   }

   action_result consolidate(const account_name& owner) {
      return push_action(name(owner), "consolidate"_n, mvo()("owner", owner));
   }

   action_result mvtosavings(const account_name& owner, const asset& rex) {
      return push_action(name(owner), "mvtosavings"_n, mvo()("owner", owner)("rex", rex));
   }

   action_result mvfrsavings(const account_name& owner, const asset& rex) {
      return push_action(name(owner), "mvfrsavings"_n, mvo()("owner", owner)("rex", rex));
   }

   action_result closerex(const account_name& owner) {
      return push_action(name(owner), "closerex"_n, mvo()("owner", owner));
   }

   action_result setrexmature(const std::optional<uint32_t> num_of_maturity_buckets,
                              const std::optional<bool>     sell_matured_rex,
                              const std::optional<bool>     buy_rex_to_savings) {
      return push_action("eosio"_n, "setrexmature"_n,
                         mvo()("num_of_maturity_buckets", num_of_maturity_buckets)(
                            "sell_matured_rex", sell_matured_rex)("buy_rex_to_savings", buy_rex_to_savings));
   }

   action_result donatetorex(const account_name& payer, const asset& quantity, const std::string& memo) {
      return push_action(name(payer), "donatetorex"_n, mvo()("payer", payer)("quantity", quantity)("memo", memo));
   }

   fc::variant get_last_loan(bool cpu) {
      vector<char> data;
      const auto&  db   = control->db();
      namespace chain   = eosio::chain;
      auto        table = cpu ? "cpuloan"_n : "netloan"_n;
      const auto* t_id  = db.find<eosio::chain::table_id_object, chain::by_code_scope_table>(
         boost::make_tuple(config::system_account_name, config::system_account_name, table));
      if (!t_id) {
         return fc::variant();
      }

      const auto& idx = db.get_index<chain::key_value_index, chain::by_scope_primary>();

      auto itr = idx.upper_bound(boost::make_tuple(t_id->id, std::numeric_limits<uint64_t>::max()));
      if (itr == idx.begin()) {
         return fc::variant();
      }
      --itr;
      if (itr->t_id != t_id->id) {
         return fc::variant();
      }

      data.resize(itr->value.size());
      memcpy(data.data(), itr->value.data(), data.size());
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_loan", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_last_cpu_loan() { return get_last_loan(true); }

   fc::variant get_last_net_loan() { return get_last_loan(false); }

   fc::variant get_loan_info(const uint64_t& loan_num, bool cpu) const {
      name         table_name = cpu ? "cpuloan"_n : "netloan"_n;
      vector<char> data       = get_row_by_account(config::system_account_name, config::system_account_name, table_name,
                                                   account_name(loan_num));
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_loan", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_cpu_loan(const uint64_t loan_num) const { return get_loan_info(loan_num, true); }

   fc::variant get_net_loan(const uint64_t loan_num) const { return get_loan_info(loan_num, false); }

   fc::variant get_dbw_obj(const account_name& from, const account_name& receiver) const {
      vector<char> data = get_row_by_account(config::system_account_name, from, "delband"_n, receiver);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("delegated_bandwidth", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   asset get_rex_balance(const account_name& act) const {
      vector<char> data = get_row_by_account(config::system_account_name, config::system_account_name, "rexbal"_n, act);
      return data.empty()
                ? asset(0, symbol(SY(4, REX)))
                : abi_ser
                     .binary_to_variant("rex_balance", data,
                                        abi_serializer::create_yield_function(abi_serializer_max_time))["rex_balance"]
                     .as<asset>();
   }

   fc::variant get_rex_balance_obj(const account_name& act) const {
      vector<char> data = get_row_by_account(config::system_account_name, config::system_account_name, "rexbal"_n, act);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_balance", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   asset get_rex_fund(const account_name& act) const {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "rexfund"_n, act);
      return data.empty()
                ? asset(0, symbol{CORE_SYM})
                : abi_ser
                     .binary_to_variant("rex_fund", data,
                                        abi_serializer::create_yield_function(abi_serializer_max_time))["balance"]
                     .as<asset>();
   }

   fc::variant get_rex_fund_obj(const account_name& act) const {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "rexfund"_n, act);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_fund", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   asset get_rex_vote_stake(const account_name& act) const {
      vector<char> data = get_row_by_account(config::system_account_name, config::system_account_name, "rexbal"_n, act);
      return data.empty()
                ? eos("0.0000")
                : abi_ser
                     .binary_to_variant("rex_balance", data,
                                        abi_serializer::create_yield_function(abi_serializer_max_time))["vote_stake"]
                     .as<asset>();
   }

   fc::variant get_rex_order(const account_name& act) {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "rexqueue"_n, act);
      return abi_ser.binary_to_variant("rex_order", data,
                                       abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_rex_order_obj(const account_name& act) {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "rexqueue"_n, act);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_order", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   auto get_pending_block_time() {
      return control->pending_block_time();
   }

   fc::variant get_rex_pool() const {
      vector<char> data;
      const auto&  db  = control->db();
      namespace chain  = eosio::chain;
      const auto* t_id = db.find<eosio::chain::table_id_object, chain::by_code_scope_table>(
         boost::make_tuple(config::system_account_name, config::system_account_name, "rexpool"_n));
      if (!t_id) {
         return fc::variant();
      }

      const auto& idx = db.get_index<chain::key_value_index, chain::by_scope_primary>();

      auto itr = idx.lower_bound(boost::make_tuple(t_id->id, 0));
      if (itr == idx.end() || itr->t_id != t_id->id || 0 != itr->primary_key) {
         return fc::variant();
      }

      data.resize(itr->value.size());
      memcpy(data.data(), itr->value.data(), data.size());
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_pool", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_rex_return_pool() const {
      vector<char> data;
      const auto&  db  = control->db();
      namespace chain  = eosio::chain;
      const auto* t_id = db.find<eosio::chain::table_id_object, chain::by_code_scope_table>(
         boost::make_tuple(config::system_account_name, config::system_account_name, "rexretpool"_n));
      if (!t_id) {
         return fc::variant();
      }

      const auto& idx = db.get_index<chain::key_value_index, chain::by_scope_primary>();

      auto itr = idx.lower_bound(boost::make_tuple(t_id->id, 0));
      if (itr == idx.end() || itr->t_id != t_id->id || 0 != itr->primary_key) {
         return fc::variant();
      }

      data.resize(itr->value.size());
      memcpy(data.data(), itr->value.data(), data.size());
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_return_pool", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_rex_return_buckets() const {
      vector<char> data;
      const auto&  db  = control->db();
      namespace chain  = eosio::chain;
      const auto* t_id = db.find<eosio::chain::table_id_object, chain::by_code_scope_table>(
         boost::make_tuple(config::system_account_name, config::system_account_name, "retbuckets"_n));
      if (!t_id) {
         return fc::variant();
      }

      const auto& idx = db.get_index<chain::key_value_index, chain::by_scope_primary>();

      auto itr = idx.lower_bound(boost::make_tuple(t_id->id, 0));
      if (itr == idx.end() || itr->t_id != t_id->id || 0 != itr->primary_key) {
         return fc::variant();
      }

      data.resize(itr->value.size());
      memcpy(data.data(), itr->value.data(), data.size());
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("rex_return_buckets", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   void setup_rex_accounts(const std::vector<account_name>& accounts, const asset& init_balance,
                           const asset& net = eos("80.0000"),
                           const asset& cpu = eos("80.0000"), bool deposit_into_rex_fund = true) {
      const asset nstake = eos("10.0000");
      const asset cstake = eos("10.0000");
      create_account_with_resources("proxyaccount"_n, config::system_account_name, eos("1.0000"),
                                    false, net, cpu);
      BOOST_REQUIRE_EQUAL(success(),
                          push_action("proxyaccount"_n, "regproxy"_n, mvo()("proxy", "proxyaccount")("isproxy", true)));
      for (const auto& a : accounts) {
         create_account_with_resources(a, config::system_account_name, eos("1.0000"), false, net,
                                       cpu);
         transfer(config::system_account_name, a, init_balance + nstake + cstake, config::system_account_name);
         BOOST_REQUIRE_EQUAL(success(), stake(a, a, nstake, cstake));
         BOOST_REQUIRE_EQUAL(success(), vote(a, {}, "proxyaccount"_n));
         BOOST_REQUIRE_EQUAL(init_balance, get_balance(a));
         BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 REX"), get_rex_balance(a));
         if (deposit_into_rex_fund) {
            BOOST_REQUIRE_EQUAL(success(), deposit(a, init_balance));
            BOOST_REQUIRE_EQUAL(init_balance, get_rex_fund(a));
            BOOST_REQUIRE_EQUAL(0, get_balance(a).get_amount());
         }
      }
   }

   action_result bidname(const account_name& bidder, const account_name& newname, const asset& bid) {
      return push_action(name(bidder), "bidname"_n, mvo()("bidder", bidder)("newname", newname)("bid", bid));
   }
   action_result bidname(std::string_view bidder, std::string_view newname, const asset& bid) {
      return bidname(account_name(bidder), account_name(newname), bid);
   }

   static fc::variant_object producer_parameters_example(int n) {
      return mutable_variant_object()("max_block_net_usage", 10000000 + n)("target_block_net_usage_pct", 10 + n)(
         "max_transaction_net_usage", 1000000 + n)("base_per_transaction_net_usage", 100 + n)(
         "net_usage_leeway", 500 + n)("context_free_discount_net_usage_num", 1 + n)(
         "context_free_discount_net_usage_den", 100 + n)("max_block_cpu_usage", 10000000 + n)(
         "target_block_cpu_usage_pct", 10 + n)("max_transaction_cpu_usage", 1000000 + n)(
         "min_transaction_cpu_usage", 100 + n)("max_transaction_lifetime", 3600 + n)("deferred_trx_expiration_window",
                                                                                     600 + n)(
         "max_transaction_delay", 10 * 86400 + n)("max_inline_action_size", 4096 + n)("max_inline_action_depth", 4 + n)(
         "max_authority_depth", 6 + n)("max_ram_size", (n % 10 + 1) * 1024 * 1024)("ram_reserve_ratio", 100 + n);
   }

   action_result regproducer(const account_name& acnt, int params_fixture = 1) {
      action_result r =
         push_action(acnt, "regproducer"_n,
                     mvo()("producer", acnt)("producer_key", get_public_key(acnt, "active"))("url", "")("location", 0));
      BOOST_REQUIRE_EQUAL(success(), r);
      return r;
   }

   action_result vote(const account_name& voter, const std::vector<account_name>& producers,
                      const account_name& proxy = name(0)) {
      return push_action(voter, "voteproducer"_n, mvo()("voter", voter)("proxy", proxy)("producers", producers));
   }
   action_result vote(const account_name& voter, const std::vector<account_name>& producers, std::string_view proxy) {
      return vote(voter, producers, account_name(proxy));
   }

   uint32_t last_block_time() const { return time_point_sec(control->head().block_time()).sec_since_epoch(); }

   asset get_balance(const account_name& act, symbol balance_symbol = symbol{CORE_SYM}) {
      vector<char> data =
         get_row_by_account("eosio.token"_n, act, "accounts"_n, account_name(balance_symbol.to_symbol_code().value));
      return data.empty()
                ? asset(0, balance_symbol)
                : token_abi_ser
                     .binary_to_variant("account", data,
                                        abi_serializer::create_yield_function(abi_serializer_max_time))["balance"]
                     .as<asset>();
   }

   asset get_balance(std::string_view act, symbol balance_symbol = symbol{CORE_SYM}) {
      return get_balance(account_name(act), balance_symbol);
   }

   fc::variant get_total_stake(std::string_view act) { return get_total_stake(account_name(act)); }

   fc::variant get_voter_info(const account_name& act) {
      vector<char> data = get_row_by_account(config::system_account_name, config::system_account_name, "voters"_n, act);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("voter_info", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }
   fc::variant get_voter_info(std::string_view act) { return get_voter_info(account_name(act)); }

   fc::variant get_producer_info(const account_name& act) {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "producers"_n, act);
      return abi_ser.binary_to_variant("producer_info", data,
                                       abi_serializer::create_yield_function(abi_serializer_max_time));
   }
   fc::variant get_producer_info(std::string_view act) { return get_producer_info(account_name(act)); }

   fc::variant get_producer_info2(const account_name& act) {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "producers2"_n, act);
      return abi_ser.binary_to_variant("producer_info2", data,
                                       abi_serializer::create_yield_function(abi_serializer_max_time));
   }
   fc::variant get_producer_info2(std::string_view act) { return get_producer_info2(account_name(act)); }

   void create_currency(name contract, name manager, asset maxsupply) {
      auto act = mutable_variant_object()("issuer", manager)("maximum_supply", maxsupply);

      base_tester::push_action(contract, "create"_n, contract, act);
   }

   void issue(const asset& quantity, const name& to = config::system_account_name) {
      base_tester::push_action("eosio.token"_n, "issue"_n, to,
                               mutable_variant_object()("to", to)("quantity", quantity)("memo", ""));
   }

   void retire(const asset& quantity, const name& issuer = config::system_account_name) {
      base_tester::push_action("eosio.token"_n, "retire"_n, issuer,
                               mutable_variant_object()("quantity", quantity)("memo", ""));
   }

   void issuefixed(const asset& supply, const name& to = config::system_account_name) {
      base_tester::push_action("eosio.token"_n, "issuefixed"_n, to,
                               mutable_variant_object()("to", to)("supply", supply)("memo", ""));
   }

   void setmaxsupply(const asset& maximum_supply, const name& issuer = config::system_account_name) {
      base_tester::push_action("eosio.token"_n, "setmaxsupply"_n, issuer,
                               mutable_variant_object()("issuer", issuer)("maximum_supply", maximum_supply));
   }

   void transfer_xyz(const name& from, const name& to, const asset& amount) {
      base_tester::push_action(xyz_name, "transfer"_n, from,
                               mutable_variant_object()("from", from)("to", to)("quantity", amount)("memo", ""));
   }

   void transfer(const name& from, const name& to, const asset& amount,
                 const name& manager = config::system_account_name) {
      base_tester::push_action("eosio.token"_n, "transfer"_n, manager,
                               mutable_variant_object()("from", from)("to", to)("quantity", amount)("memo", ""));
   }

   void transfer(const name& from, std::string_view to, const asset& amount,
                 const name& manager = config::system_account_name) {
      transfer(from, name(to), amount, manager);
   }

   void transfer(std::string_view from, std::string_view to, const asset& amount, std::string_view manager) {
      transfer(name(from), name(to), amount, name(manager));
   }

   void transfer(std::string_view from, std::string_view to, const asset& amount) {
      transfer(name(from), name(to), amount);
   }

   void issue_and_transfer(const name& to, const asset& amount, const name& manager = config::system_account_name) {
      signed_transaction trx;
      trx.actions.emplace_back(get_action("eosio.token"_n, "issue"_n,
                                          vector<permission_level>{
                                             {manager, config::active_name}
      },
                                          mutable_variant_object()("to", manager)("quantity", amount)("memo", "")));
      if (to != manager) {
         trx.actions.emplace_back(
            get_action("eosio.token"_n, "transfer"_n,
                       vector<permission_level>{
                          {manager, config::active_name}
         },
                       mutable_variant_object()("from", manager)("to", to)("quantity", amount)("memo", "")));
      }
      set_transaction_headers(trx);
      trx.sign(get_private_key(manager, "active"), control->get_chain_id());
      push_transaction(trx);
   }

   void issue_and_transfer(std::string_view to, const asset& amount, std::string_view manager) {
      issue_and_transfer(name(to), amount, name(manager));
   }

   void issue_and_transfer(std::string_view to, const asset& amount, const name& manager) {
      issue_and_transfer(name(to), amount, manager);
   }

   void issue_and_transfer(std::string_view to, const asset& amount) { issue_and_transfer(name(to), amount); }

   double stake2votes(asset stake) {
      auto now = control->pending_block_time().time_since_epoch().count() / 1000000;
      return stake.get_amount() * pow(2, int64_t((now - (config::block_timestamp_epoch / 1000)) / (86400 * 7)) /
                                            double(52)); // 52 week periods (i.e. ~years)
   }

   double stake2votes(const string& s) { return stake2votes(core_sym::from_string(s)); }

   fc::variant get_stats(const string& symbolname) {
      auto         symb        = eosio::chain::symbol::from_string(symbolname);
      auto         symbol_code = symb.to_symbol_code().value;
      vector<char> data = get_row_by_account("eosio.token"_n, name(symbol_code), "stat"_n, account_name(symbol_code));
      return data.empty() ? fc::variant()
                          : token_abi_ser.binary_to_variant(
                               "currency_stats", data, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   asset get_token_supply() { return get_stats("4," CORE_SYM_NAME)["supply"].as<asset>(); }

   uint64_t microseconds_since_epoch_of_iso_string(const fc::variant& v) {
      return static_cast<uint64_t>(time_point::from_iso_string(v.as_string()).time_since_epoch().count());
   }

   fc::variant get_global_state() {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "global"_n, "global"_n);
      if (data.empty())
         std::cout << "\nData is empty\n" << std::endl;
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("eosio_global_state", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_global_state2() {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "global2"_n, "global2"_n);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("eosio_global_state2", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_global_state3() {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "global3"_n, "global3"_n);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("eosio_global_state3", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_global_state4() {
      vector<char> data =
         get_row_by_account(config::system_account_name, config::system_account_name, "global4"_n, "global4"_n);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("eosio_global_state4", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_refund_request(name account) {
      vector<char> data = get_row_by_account(config::system_account_name, account, "refunds"_n, account);
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("refund_request", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   abi_serializer initialize_multisig() {
      abi_serializer msig_abi_ser;
      {
         create_account_with_resources( "eosio.msig"_n, config::system_account_name );
         BOOST_REQUIRE_EQUAL( success(), eosio.buyram( "eosio"_n, "eosio.msig"_n, core_sym::from_string("5000.0000") ) );
         produce_block();

         auto trace = base_tester::push_action(config::system_account_name, "setpriv"_n,
                                               config::system_account_name,  mutable_variant_object()
                                               ("account", "eosio.msig")
                                               ("is_priv", 1)
         );

         set_code( "eosio.msig"_n, eos_contracts::msig_wasm() );
         set_abi( "eosio.msig"_n, eos_contracts::msig_abi().data() );

         produce_blocks();
         const auto& accnt = control->db().get<account_object,by_name>( "eosio.msig"_n );
         abi_def msig_abi;
         BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, msig_abi), true);
         msig_abi_ser.set_abi(msig_abi, abi_serializer::create_yield_function(abi_serializer_max_time));
      }
      return msig_abi_ser;
   }

   vector<name> active_and_vote_producers(uint32_t num_producers = 21) {
      //stake more than 15% of total EOS supply to activate chain
      transfer( "eosio"_n, "alice1111111"_n, core_sym::from_string("650000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111"_n, "alice1111111"_n, core_sym::from_string("300000000.0000"), core_sym::from_string("300000000.0000") ) );

      // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
      std::vector<account_name> producer_names;
      {
         producer_names.reserve('z' - 'a' + 1);
         const std::string root("defproducer");
         for ( char c = 'a'; c < 'a'+num_producers; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
         setup_producer_accounts(producer_names);
         for (const auto& p: producer_names) {

            BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         }
      }
      produce_block();
      produce_block(fc::seconds(1000));

      auto trace_auth = validating_tester::push_action(config::system_account_name, updateauth::get_name(), config::system_account_name, mvo()
                                            ("account", name(config::system_account_name).to_string())
                                            ("permission", name(config::active_name).to_string())
                                            ("parent", name(config::owner_name).to_string())
                                            ("auth",  authority(1, {key_weight{get_public_key( config::system_account_name, "active" ), 1}}, {
                                                  permission_level_weight{{config::system_account_name, config::eosio_code_name}, 1},
                                                     permission_level_weight{{config::producers_account_name,  config::active_name}, 1}
                                               }
                                            ))
      );
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace_auth->receipt->status);

      //vote for producers
      {
         transfer( config::system_account_name, "alice1111111"_n, core_sym::from_string("100000000.0000"), config::system_account_name );
         BOOST_REQUIRE_EQUAL(success(), stake( "alice1111111"_n, core_sym::from_string("30000000.0000"), core_sym::from_string("30000000.0000") ) );
         BOOST_REQUIRE_EQUAL(success(), eosio.buyram( "alice1111111"_n, "alice1111111"_n, core_sym::from_string("30000000.0000") ) );
         BOOST_REQUIRE_EQUAL(success(), push_action("alice1111111"_n, "voteproducer"_n, mvo()
                                                    ("voter",  "alice1111111")
                                                    ("proxy", name(0).to_string())
                                                    ("producers", vector<account_name>(producer_names.begin(), producer_names.begin()+num_producers))
                             )
         );
      }
      produce_blocks( 2 * 21 ); // This is minimum number of blocks required by ram_gift in system_tests

      auto producer_schedule = control->active_producers();
      BOOST_REQUIRE_EQUAL( 21, producer_schedule.producers.size() );
      BOOST_REQUIRE_EQUAL( name("defproducera"), producer_schedule.producers[0].producer_name );

      return producer_names;
   }

   void cross_15_percent_threshold() {
      setup_producer_accounts({"producer1111"_n});
      regproducer("producer1111"_n);
      {
         signed_transaction trx;
         set_transaction_headers(trx);

         trx.actions.emplace_back( get_action( config::system_account_name, "delegatebw"_n,
                                               vector<permission_level>{{config::system_account_name, config::active_name}},
                                               mvo()
                                               ("from", name{config::system_account_name})
                                               ("receiver", "producer1111")
                                               ("stake_net_quantity", core_sym::from_string("150000000.0000") )
                                               ("stake_cpu_quantity", core_sym::from_string("0.0000") )
                                               ("transfer", 1 )
                                             )
                                 );
         trx.actions.emplace_back( get_action( config::system_account_name, "voteproducer"_n,
                                               vector<permission_level>{{"producer1111"_n, config::active_name}},
                                               mvo()
                                               ("voter", "producer1111")
                                               ("proxy", name(0).to_string())
                                               ("producers", vector<account_name>(1, "producer1111"_n))
                                             )
                                 );
         trx.actions.emplace_back( get_action( config::system_account_name, "undelegatebw"_n,
                                               vector<permission_level>{{"producer1111"_n, config::active_name}},
                                               mvo()
                                               ("from", "producer1111")
                                               ("receiver", "producer1111")
                                               ("unstake_net_quantity", core_sym::from_string("150000000.0000") )
                                               ("unstake_cpu_quantity", core_sym::from_string("0.0000") )
                                             )
                                 );

         set_transaction_headers(trx);
         trx.sign( get_private_key( config::system_account_name, "active" ), control->get_chain_id()  );
         trx.sign( get_private_key( "producer1111"_n, "active" ), control->get_chain_id()  );
         push_transaction( trx );
         produce_block();
      }
   }

   action_result setinflation(int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor) {
      return push_action("eosio"_n, "setinflation"_n,
                         mvo()("annual_rate", annual_rate)("inflation_pay_factor",
                                                           inflation_pay_factor)("votepay_factor", votepay_factor));
   }

   action_result setpayfactor(int64_t inflation_pay_factor, int64_t votepay_factor) {
      return push_action("eosio"_n, "setpayfactor"_n,
                         mvo()("inflation_pay_factor", inflation_pay_factor)("votepay_factor", votepay_factor));
   }

   action_result setschedule(const time_point_sec start_time, double continuous_rate) {
      return push_action("eosio"_n, "setschedule"_n,
                         mvo()("start_time", start_time)("continuous_rate", continuous_rate));
   }

   action_result delschedule(const time_point_sec start_time) {
      return push_action("eosio"_n, "delschedule"_n, mvo()("start_time", start_time));
   }

   action_result execschedule(const name executor) { return push_action(executor, "execschedule"_n, mvo()); }

   fc::variant get_vesting_schedule(uint64_t time) {
      vector<char> data = get_row_by_account("eosio"_n, "eosio"_n, "schedules"_n, account_name(time));
      return data.empty() ? fc::variant()
                          : abi_ser.binary_to_variant("schedules_info", data,
                                                      abi_serializer::create_yield_function(abi_serializer_max_time));
   }



   action_result bpay_claimrewards(const account_name owner) {
      action act;
      act.account = "eosio.bpay"_n;
      act.name    = "claimrewards"_n;
      act.data    = abi_ser.variant_to_binary(bpay_abi_ser.get_action_type("claimrewards"_n), mvo()("owner", owner),
                                              abi_serializer::create_yield_function(abi_serializer_max_time));

      return base_tester::push_action(std::move(act), owner.to_uint64_t());
   }

   fc::variant get_bpay_rewards(account_name producer) {
      vector<char> data = get_row_by_account("eosio.bpay"_n, "eosio.bpay"_n, "rewards"_n, producer);
      return data.empty() ? fc::variant()
                          : bpay_abi_ser.binary_to_variant(
                               "rewards_row", data, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   abi_serializer abi_ser;       // eos system contract
   abi_serializer token_abi_ser; // eos token contract
   abi_serializer bpay_abi_ser;
   abi_serializer xyz_abi_ser; // xyz wrap contract
};

inline fc::mutable_variant_object voter(account_name acct) {
   return mutable_variant_object()("owner", acct)("proxy", name(0).to_string())("producers", variants())("staked",
                                                                                                         int64_t(0))
      //("last_vote_weight", double(0))
      ("proxied_vote_weight", double(0))("is_proxy", 0);
}
inline fc::mutable_variant_object voter(std::string_view acct) {
   return voter(account_name(acct));
}

inline fc::mutable_variant_object voter(account_name acct, const asset& vote_stake) {
   return voter(acct)("staked", vote_stake.get_amount());
}
inline fc::mutable_variant_object voter(std::string_view acct, const asset& vote_stake) {
   return voter(account_name(acct), vote_stake);
}

inline fc::mutable_variant_object voter(account_name acct, int64_t vote_stake) {
   return voter(acct)("staked", vote_stake);
}
inline fc::mutable_variant_object voter(std::string_view acct, int64_t vote_stake) {
   return voter(account_name(acct), vote_stake);
}

inline fc::mutable_variant_object proxy(account_name acct) {
   return voter(acct)("is_proxy", 1);
}

inline uint64_t M(const string& eos_str) {
   return core_sym::from_string(eos_str).get_amount();
}

} // namespace eosio_system
