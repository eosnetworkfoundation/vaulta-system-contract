# Vaulta Token and System Docs

The XYZ (placeholder name) token is a replacement for the EOS token.

There are three primary functions of this contract:

- XYZ token
- Token swap
- System wrapper

The main contract is at `/contracts/system.entry.cpp`. 
All other cpp files are for testing purposes. 

See below for more information.

### Installing, building, and testing

To install the dependencies, run:

```bash
npm install
# or 
yarn
# or (preferred)
bun install
```

To build the contract, run:

```bash
npx fuckyea build
```

To test the contracts
    
```bash
npx fuckyea test
```

To deploy the contract, run:

```bash
npx fuckyea deploy <network>
```

### Building c++ tests

You will need [`spring`](https://github.com/AntelopeIO/spring), and [`cdt`](https://github.com/AntelopeIO/cdt).

You must have the [`eos-system-contracts`](https://github.com/eosnetworkfoundation/eos-system-contracts) locally, and built already.
Add their location to your path: 

```bash
export SYSTEM_CONTRACTS_PATH=/path/to/eos-system-contracts/build
```

Then create a build directory:

```bash
cd xyz-system-contract
mkdir build
cd build
```

Now you can build the contracts and tests.

```bash
export SPRING_BUILD_PATH=/path/to/spring/build
export CDT_BUILD_PATH=/path/to/cdt/build

cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -Dcdt_DIR="${CDT_BUILD_PATH}/lib/cmake/cdt" -Dspring_DIR="${SPRING_BUILD_PATH}/lib/cmake/spring" ..
make -j $(nproc)
```

To run the tests: 
```bash
cd /xyz-system-contract/build/tests
ctest -j $(nproc) --rerun-failed --output-on-failure
```

## XYZ Token

The XYZ token has the standard token functions and data structures.
It is embedded within the contract to reduce complexity from inlines and notification receivers.

### Actions

#### `init(asset maximum_supply)`

The `init` action initializes the token with the maximum supply (which should match the EOS token supply),
and the **new** ticker symbol. It creates the token, sets the maximum supply, and issues the initial supply to the contract account
which will later be used for swaps.

> Note: The `init` action can only be called once, and only by the contract account.
> It **must** be called before any other actions can be used.

#### `transfer(name from, name to, asset quantity, string memo)`

The `transfer` action transfers tokens from the sender to the receiver.
If the recipient account does not already have an open row in the `accounts` table, it will be created
with the `from` account as the RAM payer.

Example:
```json
{
  "from": "user1",
  "to": "user2",
  "quantity": "10.0000 XYZ",
  "memo": "Hello, world!"
}
```

#### `open(name owner, symbol symbol, name ram_payer)`

Opens a row in the `accounts` table for the specified account and symbol.
Accounts can open their own row in the token contract to allow others to transfer tokens to them
without having to pay for the RAM for them.

#### `close(name owner, symbol symbol)`

Closes the row in the `accounts` table for the specified account and symbol, freeing up RAM.
Accounts must have a zero balance in order to close their row.

## Swaps

The token swap functionality is a bidirectional 1 to 1 swap between the EOS token and the XYZ token.
Swaps are initiated by the user, and does not happen automatically.

To swap between XYZ -> EOS or EOS -> XYZ, the user must send the tokens to the contract account,
depending on the direction of the swap.

For instance, to get XYZ, the user must send EOS to the contract account.
To get EOS, the user must send XYZ to the contract account.

The contract will then send back the equivalent amount of tokens to the user.

Example:

- User sends 100 EOS to the contract account
- Contract sends back 100 XYZ to the user
- The user now has 100 XYZ and the contract has 100 EOS

### There is no slippage or fees

The swap has a 1:1 ratio, and there are no fees or slippage.
There will always be enough XYZ or EOS in the contract account to fulfill any swap, since the
contract starts with the full supply of the new token. As EOS is swapped for XYZ, the EOS balance of the contract account
will increase, and the XYZ balance will decrease by the same amount.

> Note: Any tokens that are burned on either side are locked into that side. For instance
> if EOS is burned, it can never become XYZ, and vice versa.

### Swap & Withdraw

The contract comes with a `swapto` action aimed at allowing exchanges to support withdrawing the EOS token and 
swapping it to the new token while also sending the swapped token to the user's account instead of crediting it back 
to the exchange's hot wallet. 

```cpp
swapto(
    const name& from, 
    const name& to, 
    const asset& quantity, 
    const std::string& memo
)
```

The `swapto` action is similar to the `transfer` action but based on the token you use in the `quantity` parameter,
the contract will swap the token to the other token and send it to the `to` account.

Examples:
- **Exchange** uses `swapto` with `100 EOS` as the quantity and **User** as the `to` account
- The contract swaps the `100 EOS` to `100 XYZ` and sends it to **User**

-- or --

- **Exchange** uses `swapto` with `100 XYZ` as the quantity and **User** as the `to` account
- The contract swaps the `100 XYZ` to `100 EOS` and sends it to **User**

## System Wrapper

The system wrapper is a set of actions that allows interaction with the system contracts using
the XYZ token. This is a convenience feature that allows users to interact with the system contracts
without having to swap to EOS first and add more actions to the transaction.

The actions you can perform with the system wrapper are identical to the actions on the system
contract (`eosio`), except that the token used in the `asset` parameters is the XYZ token instead of EOS.

All user-facing actions from the `eosio` account are available within this wrapper contract.


