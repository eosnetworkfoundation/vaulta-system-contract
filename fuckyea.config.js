module.exports = {
    networks:{
        jungle: {
            node_url: 'https://jungle4.greymass.com/',
            chain: 'Jungle4',
            accounts: [
                {
                    name: 'rebrandtests',
                    private_key: process.env.PRIVATE_KEY
                }
            ]
        }
    },
}
