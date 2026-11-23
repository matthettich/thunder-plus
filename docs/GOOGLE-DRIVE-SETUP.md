# Google Drive in Thunder Plus: setup (web app only)

File ▾ → **Save to Drive**, **Open latest from Drive**, **Open from Drive…** work in the web app (https://matthettich.github.io, on the phone and
in desktop Chrome). They stay greyed out in effect ("Google Drive isn't set up yet") until a Google OAuth client ID is pasted into
`DRIVE_CLIENT_ID` near the top of the app script in `index.html`. The client ID is not a secret. About 15 minutes, once.

## 1. Google Cloud project
1. Open https://console.cloud.google.com and sign in with your Google account.
2. Top bar project picker → **New project** → name it "Thunder Plus" → **Create**, then select it.
3. Menu (☰) → **APIs & Services** → **Library** → search "Google Drive API" → **Enable**.

## 2. Google Auth Platform (the consent screen)
1. Menu → **Google Auth Platform** → **Get started** (or **Branding**).
2. **Branding**: App name "Thunder Plus", User support email (yours), Developer contact email (yours) → **Save**.
3. **Audience**: choose **External**. Leave the publishing status on **Testing** (do not press "Publish app").
   Under **Test users** → **Add users** → add your own Google address and any friends' (up to 100).
   Full Drive access (`drive`) is a restricted scope, so the app stays in Testing: fine for personal use. The first time you sign in Google shows
   an "unverified app" screen: click **Advanced** → **Go to Thunder Plus (unsafe)** → tick the Drive boxes → **Continue**.
   (In Testing, Google asks you to sign in again about every 7 days. Same click-through.)
4. **Data access** → **Add or remove scopes** → add this one (paste it into "Manually add scopes" if it is not listed) → **Update** → **Save**:
   - `https://www.googleapis.com/auth/drive`
   (Full access, because the desktop app's songs and folder are made by Drive for desktop, not by this app; narrower scopes could read
   them but not save into that folder. The app only lists, reads and writes .thunder files in **My Drive / Thunder Plus**.)

## 3. The client ID
1. **Google Auth Platform** → **Clients** → **Create client** → type **Web application** → name "Thunder Plus web".
2. **Authorized JavaScript origins** → **Add URI**: `https://matthettich.github.io`
   (and `http://localhost:8080` if you test locally). Leave "Authorized redirect URIs" empty.
3. **Create**. Copy the **Client ID** (ends in `.apps.googleusercontent.com`; ignore the client secret, the app does not use it).
4. Paste it into `const DRIVE_CLIENT_ID = '';` in `index.html`, or send it to Claude to paste. Changes can take a few minutes to take effect.

## 4. The desktop side
The desktop app does not talk to Drive itself. Install **Google Drive for desktop** (https://www.google.com/drive/download/), sign in, and in the
desktop app save songs (File ▾ → Save project as…) into **My Drive / Thunder Plus** (in Finder or Explorer that is the Google Drive
folder → My Drive → Thunder Plus; create the folder there if it isn't there). Drive for desktop uploads it. The web app finds the folder
by name ("Thunder Plus" directly in My Drive) and can read those files although it did not create them.

## How it behaves
- Save to Drive writes `<song name>.thunder` into that folder, overwriting the same-named song there (after asking if Drive's copy is one this device hasn't seen); Open latest picks the newest.
- Open latest / Open from Drive… use the normal Open (the "Save changes?" question included).
- Problems (offline, sign-in closed, "403 / access blocked"): a short message; a 403 usually means the account is not on the Test users list.

## Next step (planned, owner 23 Nov): Drive in the desktop app
Not built. The Drive items are hidden in the Electron app today (`window.thunderDesktop` is set there).
- Sign-in in the Electron **main process**: open the system browser (not an in-app window; Google refuses embedded sign-in) to Google's OAuth
  page with PKCE (code challenge, no client secret), a loopback redirect `http://127.0.0.1:<random port>`, then exchange the code for access and refresh tokens; the refresh token is kept encrypted (Electron `safeStorage`).
- A separate **"Desktop app" OAuth client ID** (Google Auth Platform → Clients → Create client → type **Desktop app**; same project, same scopes, same test users; no origins to add), pasted into the Electron side.
- The same three File ▾ items, and the same Drive code in `index.html` (folder, list, save, open, newer-check), reused: only the token source changes, through an IPC bridge in `electron/preload.js` (`thunderDesktop.drive.token()` returns a fresh access token; main refreshes it).
- Testing needs a packaged build on a Mac and a Windows PC (loopback port and system-browser hand-off differ), a sign-in, a save and an open round trip with a phone, an expired-token run and a "Cancel in the browser" run.
