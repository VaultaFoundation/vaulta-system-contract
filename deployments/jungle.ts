// the `tester` injected parameter is an object with the following properties & methods:
/*
{
    accounts: [same as your config accounts],
    sessions: {[accountName]: wharfkit session object},
    deploy => (accountName, contractPath): wharfkit contract instance,
}
 */

module.exports = async (deployer) => {

    const contract = await deployer.deploy('rebrandtests', 'build/system', {
        // adds the `eosio.code` permission to the contract account's active permission
        // so that you can send inline actions from the contract in its name
        addCode: true
    }).catch(err => {
        console.error(err)
        process.exit(1);
    })

    const MAX_SUPPLY = 2100000000;
    const maxSupply = `${MAX_SUPPLY}.0000 XYZ`;
    await contract.actions.init([maxSupply]).send('rebrandtests@active');

    // do other stuff here...
}
