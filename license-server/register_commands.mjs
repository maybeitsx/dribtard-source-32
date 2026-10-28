// Registers the license-server slash commands globally on Discord.
//
// Usage:
//   $env:DISCORD_APP_ID="..."; $env:DISCORD_BOT_TOKEN="..."
//   node register_commands.mjs
//
// Reads DISCORD_APP_ID and DISCORD_BOT_TOKEN from environment variables only.
// Never hardcode them. The bot token is only used here to register commands;
// it is NOT used by the Worker and must never ship in the C++ app.

const APP_ID = process.env.DISCORD_APP_ID;
const BOT_TOKEN = process.env.DISCORD_BOT_TOKEN;

if (!APP_ID || !BOT_TOKEN)
{
    console.error("Missing DISCORD_APP_ID or DISCORD_BOT_TOKEN environment variables.");
    process.exit(1);
}

const commands = [
    {
        name: "genkey",
        description: "Generate license key(s)",
        options: [
            {
                name: "days",
                description: "Days until expiry (omit = lifetime)",
                type: 4, // INTEGER
                required: false,
                min_value: 1,
                max_value: 3650,
            },
            {
                name: "count",
                description: "How many keys to create (1-10, default 1)",
                type: 4, // INTEGER
                required: false,
                min_value: 1,
                max_value: 10,
            },
            {
                name: "note",
                description: "Optional note stored with the key(s)",
                type: 3, // STRING
                required: false,
            },
        ],
    },
    {
        name: "revoke",
        description: "Revoke a license key",
        options: [
            {
                name: "key",
                description: "The license key to revoke",
                type: 3, // STRING
                required: true,
            },
        ],
    },
    {
        name: "resethwid",
        description: "Clear the hardware binding for a key",
        options: [
            {
                name: "key",
                description: "The license key to unbind",
                type: 3, // STRING
                required: true,
            },
        ],
    },
    {
        name: "keyinfo",
        description: "Show status, expiry and binding for a key",
        options: [
            {
                name: "key",
                description: "The license key to inspect",
                type: 3, // STRING
                required: true,
            },
        ],
    },
    {
        name: "listkeys",
        description: "List up to 25 keys with status",
    },
];

const res = await fetch(
    `https://discord.com/api/v10/applications/${APP_ID}/commands`,
    {
        method: "PUT",
        headers: {
            "Authorization": `Bot ${BOT_TOKEN}`,
            "Content-Type": "application/json",
        },
        body: JSON.stringify(commands),
    });

const text = await res.text();

if (!res.ok)
{
    console.error(`Failed (${res.status}): ${text}`);
    process.exit(1);
}

console.log(`Registered ${commands.length} global commands:`);
for (const c of commands)
    console.log("  /" + c.name);
console.log("Note: global commands can take up to ~1 hour to appear in Discord.");
