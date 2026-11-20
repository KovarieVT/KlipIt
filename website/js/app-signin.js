// Sign-in for the KlipIt desktop app.
//
// The app's "Sign in" button opens login.html?app=<port>&state=<state>&challenge=<challenge>.
// The member logs in here as usual (the security check included). Instead of signing this page
// in, the one-shot session token is turned into an authorization code bound to the APP's PKCE
// challenge, and the browser goes to http://127.0.0.1:<port>/callback with that code. Only the
// app holds the matching verifier, so only the app can trade the code for its own member tokens.
// The browser's own website sign-in is left exactly as it was.
import { WIX_CLIENT_ID } from "/js/wix/config.js";
import { useTokenStore, wixRequest } from "/js/wix/client.js";
import { authorizationCode, setSessionHandler } from "/js/wix/auth.js";

const B64URL = /^[A-Za-z0-9_-]{32,128}$/;

/** The app's request from the page URL, or null when this is a normal website login. */
export function appRequest(params) {
  const port = Number(params.get("app"));
  const state = params.get("state") || "";
  const challenge = params.get("challenge") || "";
  if (!Number.isInteger(port) || port < 1024 || port > 65535) return null;
  if (!B64URL.test(state) || !B64URL.test(challenge)) return null;
  return { port: port, state: state, challenge: challenge };
}

/** Routes this page's sign-in to the app. Returns the function that sends the browser back to it. */
export function startAppSignIn(req) {
  // A separate in-memory session for this page, so the website's own sign-in is untouched.
  let tokens = null;
  useTokenStore({ load() { return tokens; }, save(t) { tokens = t; } });

  let code = null;
  setSessionHandler(async function (sessionToken) {
    const body = {
      auth: {
        authRequest: {
          clientId: WIX_CLIENT_ID,
          codeChallenge: req.challenge,
          codeChallengeMethod: "S256",
          responseMode: "web_message",
          responseType: "code",
          scope: "offline_access",
          state: req.state,
          sessionToken: sessionToken,
        },
        prompt: "none",
      },
    };
    const res = await wixRequest("/headless/v1/redirect-session", { body: body });
    const url = res && res.redirectSession && res.redirectSession.fullUrl;
    if (!url) throw new Error("Sign-in couldn't finish. Try again.");
    code = await authorizationCode(url, req.state);
  });

  return function handBack() {
    if (!code) return false;
    location.assign("http://127.0.0.1:" + req.port + "/callback?code=" + encodeURIComponent(code)
      + "&state=" + encodeURIComponent(req.state));
    return true;
  };
}
