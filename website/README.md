# Website side of the app sign-in

The KlipIt website is a Wix Headless site. The app's **Sign in** button opens
`login.html?app=<port>&state=<state>&challenge=<challenge>` on that site.

- `js/app-signin.js` (copied here) is live on the site. After the member logs in, it turns
  Wix's one-time session into an authorization code bound to the app's PKCE challenge and
  sends the browser to `http://127.0.0.1:<port>/callback`. The site's own sign-in is untouched.
- On the site, `js/login.js` loads it when those parameters are present, and `js/wix/auth.js`
  exports `setSessionHandler` and `authorizationCode` for it.

The app then uploads clips with the member's own sign-in: a one-time Wix upload link per file,
then a private entry in the `Clips` collection (only that member can read it). No server and
no secret keys are involved.
