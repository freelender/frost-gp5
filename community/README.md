# Frost community

The Community page of the website shares loops, presets, amp models (.nam, .fcm) and cabinets (.wav) between GP-5
players, each with the nickname of who made it, **without accounts**. A loop can be heard, recorded over in the Looper
(the take is shared back as a reply) and sent as a link that plays in any browser.

This folder is the server: one Cloudflare Worker ([worker.js](worker.js)) with a D1 database and a file store
(Workers KV or R2). It runs on Cloudflare's free plan; nothing has to stay on at home.

## How it works

- **Identity without login.** The browser makes a signing key (ECDSA P-256) once and keeps it
  ([web/community.js](../web/community.js)). Every upload is signed with it. The author code is a hash of the public
  key, and a nickname belongs to the first key that uses it, so nobody can publish under someone else's name. The
  Community page can save the key to a file and open it on another computer.
- **Moderation.** The first upload of each author waits for the owner's approval; after that the author publishes at
  once. A report hides an item immediately, until the owner restores or deletes it (an item the owner restored stays
  up after further reports). The owner can ban an author: their items are hidden and their key can no longer publish.
  The owner is whoever holds a key whose author code is listed in `ADMIN_KEYS`, and sees an **Approvals** tab.
- **Limits.** 30 uploads a day per author, 60 per address, 20 reports a day per address, three uploads waiting per new
  author. Addresses are kept only as salted hashes. Loops up to 20 MB (WAV, about 1 min 50 s of stereo), presets
  256 KB, models 4 MB, cabinets 2 MB; each file is checked to be what its kind says.
- **Public page.** `/i/<id>` plays the item in any browser, in Portuguese or English, with a link back to the site
  (`?item=<id>#community`) and a report button. It sets Open Graph tags, so links show a preview in chat apps.

## Try it on this computer

```
node community/dev.mjs
```

The same `worker.js` runs on Node's built-in SQLite and a folder (`community/.local`, ignored by git), at
`http://127.0.0.1:8096`. The website served from `localhost` uses it automatically while the community is not online.
To be the owner locally, put your author code (shown on the Community page) in `community/.local/admin.txt`.
The server stops after 4 hours without requests. Tests: `node tests/js/community.mjs`.

## Put it online (the owner does the account steps)

1. Create a free Cloudflare account.
2. In this folder: `npx wrangler login` (opens the browser to authorise).
3. `npx wrangler d1 create frost-community`, then paste the `database_id` into [wrangler.toml](wrangler.toml).
4. Files: `npx wrangler kv namespace create frost-files` and paste its id (no card needed), or turn R2 on in
   the dashboard and use the R2 lines in `wrangler.toml` instead (10 GB free).
5. `npx wrangler secret put ADMIN_KEYS` with your author code, and `npx wrangler secret put SALT` with any long
   random text.
6. `npx wrangler deploy`. It prints the address, like `https://frost-community.<account>.workers.dev`.
7. Put that address in `ONLINE` at the top of [web/community.js](../web/community.js) and publish the website.
