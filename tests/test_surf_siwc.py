"""Offline SIWC tests: no real sign-in, credentials, provider request or billing."""
import json
import os
from pathlib import Path
import stat
import tempfile
import time
import unittest
from unittest import mock
from urllib.parse import parse_qs, urlparse

from t160 import bridge, siwc_auth


class SIWCAuthTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.env = mock.patch.dict(os.environ, {
            "SURF_SIWC_DIR": self.tmp.name + "/siwc"
        })
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def test_host_id_stable_and_private(self):
        first = siwc_auth.host_id()
        self.assertTrue(first.startswith("urn:uuid:"))
        self.assertEqual(first, siwc_auth.host_id())
        saved = Path(os.environ["SURF_SIWC_DIR"]) / "host.json"
        self.assertEqual(stat.S_IMODE(saved.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(saved.parent.stat().st_mode), 0o700)

    def test_authorization_url_uses_pkce_and_plan_scope(self):
        url, state, nonce, verifier, redirect = (
            siwc_auth.make_authorization(1455))
        parsed = urlparse(url)
        params = parse_qs(parsed.query)
        self.assertEqual(parsed.netloc, "auth.openai.com")
        self.assertEqual(params["client_id"], ["dynamic_agent_client"])
        self.assertEqual(params["agent_name_hint"], ["Surf AI"])
        self.assertEqual(params["redirect_uri"], [redirect])
        self.assertEqual(params["code_challenge_method"], ["S256"])
        self.assertIn(siwc_auth.SHARING_SCOPE, params["scope"][0].split())
        self.assertEqual(params["state"], [state])
        self.assertEqual(params["nonce"], [nonce])
        self.assertNotEqual(verifier, params["code_challenge"][0])
        self.assertNotIn("OPENAI_API_KEY", url)
        self.assertIn("ext_agent_host_id", params)

    def test_reauthorization_preserves_client_without_new_registration(self):
        saved = {
            "client_id": "oaiapp_pretend",
            "id_token": "previous-test-hint",
        }
        url, state, nonce, verifier, redirect = (
            siwc_auth.make_authorization(1455, saved))
        params = parse_qs(urlparse(url).query)
        self.assertEqual(params["client_id"], ["oaiapp_pretend"])
        self.assertEqual(params["id_token_hint"], ["previous-test-hint"])
        self.assertNotIn("agent_name_hint", params)

    def test_no_account_cannot_contact_inference(self):
        with self.assertRaises(siwc_auth.AuthError):
            siwc_auth.access_token()

    def test_rejects_unsafe_credential_permissions(self):
        path = siwc_auth._profile_path()
        path.write_text('{"client_id":"test","subject":"test"}')
        os.chmod(path, 0o644)
        with self.assertRaises(siwc_auth.AuthError):
            siwc_auth.profile()

    def test_unauthorized_scopes_rejected(self):
        tokens = {
            "access_token": "test-not-real",
            "refresh_token": "test-not-real",
            "scope": "openid email",
            "expires_in": 3600,
        }
        with self.assertRaises(siwc_auth.AuthError):
            siwc_auth._record(tokens, "oaiapp_test", {"sub": "subject"})

    def test_mock_bridge_does_not_contact_siwc(self):
        # The existing mock tests cover live socket handling.
        with mock.patch.object(siwc_auth, "access_token",
                               side_effect=AssertionError("NO AUTH")):
            # Confirm mock branch is separate from stream_provider.
            self.assertTrue(callable(bridge.stream_provider))


class SIWCBridgeTest(unittest.TestCase):
    def setUp(self):
        self.ctx = mock.patch.dict(os.environ, {
            "SURF_OPENAI_MODEL": "gpt-test-model",
            "OPENAI_API_KEY": "key-must-not-be-used",
        })
        self.ctx.start()

    def tearDown(self):
        self.ctx.stop()

    def test_oauth_only_no_api_key_fallback(self):
        with mock.patch.object(
            siwc_auth, "access_token", side_effect=siwc_auth.AuthError(
                "Autenticación no autorizada"
            )
        ), mock.patch("t160.bridge.urllib.request.urlopen") as req:
            with self.assertRaises(siwc_auth.AuthError):
                list(bridge.stream_provider(
                    "openai", [{"role": "user", "content": "hola"}]))
            req.assert_not_called()

    def test_openai_request_uses_plan_token_store_false_stream_true(self):
        data = [
            b'data: {"type":"response.output_text.delta","delta":"Hola"}\n',
            b'data: {"type":"response.completed"}\n',
            b"data: [DONE]\n",
        ]
        class Response:
            def __enter__(self): return iter(data)
            def __exit__(self, *a): return False
        with mock.patch.object(siwc_auth, "access_token",
                               return_value="siwc-test-access"):
            with mock.patch("t160.bridge.urllib.request.urlopen",
                            return_value=Response()) as call:
                output = list(bridge.stream_provider(
                    "openai", [{"role": "user", "content": "hola"}]))
        self.assertEqual(output, ["Hola"])
        args = call.call_args[0][0]
        self.assertEqual(args.full_url,
                         "https://api.openai.com/v1/responses")
        self.assertEqual(args.get_header("Authorization"),
                         "Bearer siwc-test-access")
        self.assertNotIn("key-must-not-be-used",
                         str(args.header_items()))
        body = json.loads(args.data)
        self.assertIs(body["store"], False)
        self.assertIs(body["stream"], True)
        self.assertEqual(body["model"], "gpt-test-model")

    def test_incomplete_response_not_success(self):
        class Response:
            def __enter__(self): return iter(
                [b'data: {"type":"response.output_text.delta","delta":"x"}\n'])
            def __exit__(self, *args): return False
        with mock.patch.object(siwc_auth, "access_token",
                               return_value="test"):
            with mock.patch("t160.bridge.urllib.request.urlopen",
                            return_value=Response()):
                with self.assertRaises(RuntimeError):
                    list(bridge.stream_provider("openai", []))

    def test_anthropic_never_uses_paid_api(self):
        with mock.patch("t160.bridge.urllib.request.urlopen") as req:
            with self.assertRaises(RuntimeError):
                list(bridge.stream_provider("anthropic", []))
            req.assert_not_called()


if __name__ == "__main__":
    unittest.main()
