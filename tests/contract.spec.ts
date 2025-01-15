const { Blockchain, nameToBigInt, symbolCodeToBigInt, expectToThrow } = require("@eosnetwork/vert");
const { Asset, Name } = require("@greymass/eosio");
const { assert } = require("chai");
const blockchain = new Blockchain()

const systemWrapper = blockchain.createContract('system', 'build/system', true, {privileged: true})
const system = blockchain.createContract('eosio', 'build/mocksys', true, {privileged: true})
const eosToken = blockchain.createContract('eosio.token', 'build/token', true)

const accounts = [
    'issuer',
    'swapper',
    'hacker',
    'user'
]
blockchain.createAccounts(...accounts);

const MAX_SUPPLY = 2100000000;

const setupToken = async (contract, ticker, accountBalances) => {
    const maximumSupply = `${MAX_SUPPLY}.0000 ` + ticker;

    const contractName = contract.name.toString();
    const contractIntName = nameToBigInt(contractName);

    {
        await contract.actions.create(['issuer', maximumSupply]).send(`${contractName}@active`)
        const result = await contract.tables.stat(symbolCodeToBigInt(Asset.SymbolCode.from(ticker))).getTableRows()[0];
        assert(result.max_supply === maximumSupply, "Invalid max supply");
        assert(result.issuer === 'issuer', "Invalid issuer");
        assert(result.supply === '0.0000 ' + ticker, "Invalid supply");
    }
    {
        await contract.actions.issue(['issuer', maximumSupply, '']).send(`issuer@active`)
        const result = await contract.tables.accounts(nameToBigInt('issuer')).getTableRows()[0];
        assert(result.balance === maximumSupply, "Invalid balance");
    }

    for (const {account, amount} of accountBalances) {
        await contract.actions.transfer(['issuer', account, `${amount.toFixed(4)} ${ticker}`, '']).send('issuer@active')
        const result = await contract.tables.accounts(nameToBigInt(account)).getTableRows()[0];
        assert(result.balance === `${amount.toFixed(4)} ${ticker}`, "Invalid balance");
    }
}

const checkLastAction = async (actionName) => {
    const result = await system.tables.lastaction(nameToBigInt('eosio')).getTableRows()[0];
    assert(result.action === actionName, `Invalid action: ${result.action} != ${actionName}`);
}

describe('New token changes', () => {

    it('Should be able to set up tokens', async () => {
        await setupToken(eosToken, 'EOS', [
            {account: 'swapper', amount: 100},
            {account: 'user', amount: 10000},
            {account: 'eosio', amount: 10000} // used instead of system holding accounts like the names account
        ]);
    });

    it('Should initialize the system wrapper', async () => {

        const maxSupply = `${MAX_SUPPLY}.0000 XYZ`;

        // Should only be able to initialize the system contract with the _self account
        await expectToThrow(
            systemWrapper.actions.init([maxSupply]).send('user@active'),
            "missing required authority system"
        );

        await systemWrapper.actions.init([maxSupply]).send('system@active');
    });

    it('Should be able to swap EOS for XYZ', async () => {
        await eosToken.actions.transfer(['swapper', 'system', '10.0000 EOS', '']).send('swapper@active')
        const eosBalance = await eosToken.tables.accounts(nameToBigInt('swapper')).getTableRows()[0];
        const xyzBalance = await systemWrapper.tables.accounts(nameToBigInt('swapper')).getTableRows()[0];

        assert(eosBalance.balance === '90.0000 EOS', "Invalid balance");
        assert(xyzBalance.balance === '10.0000 XYZ', "Invalid balance");
    });

    it('Should be able to swap XYZ for EOS', async () => {
        await systemWrapper.actions.transfer(['swapper', 'system', '1.0000 XYZ', '']).send('swapper@active')
        const eosBalance = await eosToken.tables.accounts(nameToBigInt('swapper')).getTableRows()[0];
        const xyzBalance = await systemWrapper.tables.accounts(nameToBigInt('swapper')).getTableRows()[0];

        assert(eosBalance.balance === '91.0000 EOS', "Invalid balance");
        assert(xyzBalance.balance === '9.0000 XYZ', "Invalid balance");

        {
            // swapping some EOS to XYZ for user
            await eosToken.actions.transfer(['user', 'system', '10000.0000 EOS', '']).send('user@active')
        }
    });

    it('Should NOT be able to swap XYZ you do not have', async () => {
        await expectToThrow(
            systemWrapper.actions.transfer(['hacker', 'system', '100.0000 XYZ', '']).send('hacker@active'),
            "eosio_assert: no balance object found"
        );
    });

    it('Should be able to automatically swap tokens and use system contracts', async () => {
        const oldXYZBalance = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        await systemWrapper.actions.bidname(['user', 'user', '1.0000 XYZ']).send('user@active');
        await checkLastAction('bidname');

        // should have consumed 1 EOS but have sent 1 XYZ
        const xyzBalance = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        assert(xyzBalance.balance.split(' ')[0] == oldXYZBalance.balance.split(' ')[0] - 1, "Invalid balance");
    });

    it('Should be able to bidrefund', async () => {
        await system.actions.insertrefund(['user', 'testing']).send('eosio@active');

        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.bidrefund(['user', 'testing']).send('user@active');
        await checkLastAction('bidrefund');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        assert(
            parseFloat(balanceBefore.balance.split(' ')[0]) + 1
            ==
            parseFloat(balanceAfter.balance.split(' ')[0]),
            "Invalid balance"
        );
    });

    it('Should be able to buyram', async () => {
        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.buyram(['user', 'user', '1.0000 XYZ']).send('user@active');
        await checkLastAction('buyram');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];


        assert(
            parseFloat(balanceBefore.balance.split(' ')[0]) - 1
            ==
            parseFloat(balanceAfter.balance.split(' ')[0]),
            `Invalid balance: ${balanceBefore.balance} / ${balanceAfter.balance}`
        );
    });

    it('Should be able to buyramself', async () => {
        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.buyramself(['user', '1.0000 XYZ']).send('user@active');
        await checkLastAction('buyramself');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];


        assert(
            parseFloat(balanceBefore.balance.split(' ')[0]) - 1
            ==
            parseFloat(balanceAfter.balance.split(' ')[0]),
            `Invalid balance: ${balanceBefore.balance} / ${balanceAfter.balance}`
        );
    });

    it('Should be able to buyramburn', async () => {
        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.buyramburn(['user', '1.0000 XYZ', '']).send('user@active');
        await checkLastAction('buyramburn');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];


        assert(
            parseFloat(balanceBefore.balance.split(' ')[0]) - 1
            ==
            parseFloat(balanceAfter.balance.split(' ')[0]),
            `Invalid balance: ${balanceBefore.balance} / ${balanceAfter.balance}`
        );
    });

    it('Should be able to buyrambytes', async () => {
        // set the ram market first
        await system.actions.setrammarket(['85450299267 RAM', '22319041.7222 EOS']).send('eosio@active');

        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.buyrambytes(['user', 'user', 1000]).send('user@active');
        await checkLastAction('buyram');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        assert(
            parseFloat(balanceBefore.balance.split(' ')[0])
            >
            parseFloat(balanceAfter.balance.split(' ')[0]),
            `Invalid balance: ${balanceBefore.balance} / ${balanceAfter.balance}`
        );
    });

    it('Should be able to burn ram', async () => {
        await systemWrapper.actions.ramburn(['user', 1000, '']).send('user@active');
    });


    it('Should be able to sell ram', async () => {
        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.sellram(['user', 1000]).send('user@active');
        await checkLastAction('sellram');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        assert(
            parseFloat(balanceBefore.balance.split(' ')[0])
            <
            parseFloat(balanceAfter.balance.split(' ')[0]),
            `Invalid balance: ${balanceBefore.balance} / ${balanceAfter.balance}`
        );
    });

    it('Should be able to stake', async () => {
        // need to set rex params first (these were the values on mainnet at the time)
        await system.actions.setrex(['130094250.8095 EOS', '1081803903132.8963 REX']).send('eosio@active');

        const balanceBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        await systemWrapper.actions.deposit(['user', '1.0000 XYZ']).send('user@active');
        await systemWrapper.actions.buyrex(['user', '1.0000 XYZ']).send('user@active');
        await checkLastAction('buyrex');
        const balanceAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        assert(
            parseFloat(balanceBefore.balance.split(' ')[0]) - 1
            ==
            parseFloat(balanceAfter.balance.split(' ')[0]),
            `Invalid balance: ${balanceBefore.balance} / ${balanceAfter.balance}`
        );

        const rexDeposited = await system.tables.staked(nameToBigInt('eosio')).getTableRows()[0];
        assert(rexDeposited.account === 'user', "Invalid owner");
        assert(rexDeposited.balance === '8315.5396 REX', "Invalid REX amount");
    });

    it('Should be able to unstake', async () => {
        await systemWrapper.actions.mvfrsavings(['user', '8315.5396 REX']).send('user@active');
        await checkLastAction('mvfrsavings');

        const staked = await system.tables.staked(nameToBigInt('eosio')).getTableRows()[0];
        assert(staked.balance === '0.0000 REX', "Did not remove staked amount");
        const unstaking = await system.tables.unstaking(nameToBigInt('eosio')).getTableRows()[0];
        assert(unstaking.account === 'user', "Invalid owner");
        assert(unstaking.balance === '8315.5396 REX', "Invalid REX amount");
    });

    it('Should be able to claim rewards', async () => {
        const xyzBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        const eosBefore = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        await systemWrapper.actions.sellrex(['user', '8315.5396 REX']).send('user@active');
        await systemWrapper.actions.withdraw(['user', '1.0000 XYZ']).send('user@active');

        const xyzAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
        const eosAfter = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

        assert(parseFloat(xyzBefore.balance.split(' ')[0]) +1 == parseFloat(xyzAfter.balance.split(' ')[0]), "Invalid eos balance");
        assert(parseFloat(eosBefore.balance.split(' ')[0]) == parseFloat(eosAfter.balance.split(' ')[0]), "Invalid xyz balance");

    });

    it('Should be able to powerup and get overages back in XYZ', async () => {
        // max_payment should be taken entirely, so nothing to convert back to XYZ
        {
            const xyzBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosBefore = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            // const name& payer, const name& receiver, uint32_t days, int64_t net_frac, int64_t cpu_frac, const asset& max_payment
            await systemWrapper.actions.powerup(['user', 'user', 1, 0, 0, '0.1000 XYZ']).send('user@active');
            await checkLastAction('powerup');

            const xyzAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosAfter = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            assert(parseFloat(xyzBefore.balance.split(' ')[0]) - 0.1 == parseFloat(xyzAfter.balance.split(' ')[0]), "Invalid balance");
            assert(parseFloat(eosBefore.balance.split(' ')[0]) == parseFloat(eosAfter.balance.split(' ')[0]), "Invalid balance");
        }

        // max_payment has overage, so should be converted back to XYZ
        {
            const xyzBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosBefore = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            // should only take 1 EOS and convert the remaining 0.1 EOS to XYZ and give it back to the user
            await systemWrapper.actions.powerup(['user', 'user', 1, 0, 0, '1.1000 XYZ']).send('user@active');
            await checkLastAction('powerup');

            const xyzAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosAfter = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            assert(parseFloat(xyzBefore.balance.split(' ')[0]) - 1.0 == parseFloat(xyzAfter.balance.split(' ')[0]), "Invalid balance");
            assert(parseFloat(eosBefore.balance.split(' ')[0]) == parseFloat(eosAfter.balance.split(' ')[0]), "Invalid balance");
        }

    });

    it('Should be able to delegate and undelegate bw, and refund delegated funds', async () => {
        // delegatebw
        {
            const xyzBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosBefore = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            await systemWrapper.actions.delegatebw(['user', 'user', '1.0000 XYZ', '1.0000 XYZ', true]).send('user@active');
            await checkLastAction('delegatebw');

            const xyzAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosAfter = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            assert(parseFloat(xyzBefore.balance.split(' ')[0]) - 2.0 == parseFloat(xyzAfter.balance.split(' ')[0]), "Invalid balance");
            assert(parseFloat(eosBefore.balance.split(' ')[0]) == parseFloat(eosAfter.balance.split(' ')[0]), "Invalid balance");
        }

        // undelegatebw
        {
            const xyzBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosBefore = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            await systemWrapper.actions.undelegatebw(['user', 'user', '1.0000 XYZ', '1.0000 XYZ']).send('user@active');
            await checkLastAction('undelegatebw');

            const xyzAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosAfter = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            assert(parseFloat(xyzBefore.balance.split(' ')[0]) == parseFloat(xyzAfter.balance.split(' ')[0]), "Invalid balance");
            assert(parseFloat(eosBefore.balance.split(' ')[0]) == parseFloat(eosAfter.balance.split(' ')[0]), "Invalid balance");
        }

        // refund
        {
            const xyzBefore = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosBefore = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            await systemWrapper.actions.refund(['user']).send('user@active');
            await checkLastAction('refund');

            const xyzAfter = await systemWrapper.tables.accounts(nameToBigInt('user')).getTableRows()[0];
            const eosAfter = await eosToken.tables.accounts(nameToBigInt('user')).getTableRows()[0];

            assert(parseFloat(xyzBefore.balance.split(' ')[0]) + 2 == parseFloat(xyzAfter.balance.split(' ')[0]), "Invalid balance");
            assert(parseFloat(eosBefore.balance.split(' ')[0]) == parseFloat(eosAfter.balance.split(' ')[0]), "Invalid balance");
        }
    });

});
