"""
hermes.py — AIAvatarKit server example with Hermes Agent harness.

This is the standard AIAvatarKit pipeline (VAD -> STT -> LLM -> TTS) with
Hermes Agent (https://github.com/nousresearch/hermes-agent) wired in as the
autonomous task-execution agent via OpenClawTool's built-in "hermes" harness.

Architecture:
  StackChan --[WebSocket]--> this server
                                  |-- VAD (Silero) -> STT (OpenAI Whisper)
                                  |-- LLM (ChatGPTService, main conversation brain)
                                  |-- TTS (VOICEVOX)
                                  +-- Hermes Agent (OpenAI-compatible api) as tool
                                      -> web search / data analysis / code exec / PC ops

When the main LLM decides a user request needs autonomous task execution, it
delegates to Hermes Agent through `send_query_to_openclaw` (hermes harness).
Hermes Agent runs wherever HERMES_BASE_URL points — e.g. your own local
api_server (http://127.0.0.1:8000) or a remote instance.

Environment variables:
  OPENAI_API_KEY     Required. Used by STT (Whisper) + alphabet->kana TTS preproc.
  LLM_API_KEY        Key for the main LLM brain. Default falls back to OPENCODE_GO_API_KEY / OPENAI_API_KEY.
  LLM_BASE_URL       OpenAI-compatible base URL of the main LLM brain (default https://opencode.ai/zen/go/v1).
  LLM_MODEL          Main LLM model id (default deepseek-v4-flash).
  HERMES_API_KEY     API key for the Hermes Agent api_server (the gateway API_SERVER_KEY).
  HERMES_BASE_URL    OpenAI-compatible base URL of Hermes Agent, INCLUDING /v1.
                     e.g. http://127.0.0.1:8642/v1  (default matches a tested Hermes).

Also make sure VOICEVOX is running at http://127.0.0.1:50021 for Japanese speech.

Run:
  python -m uvicorn hermes:app --host 0.0.0.0 --port 8000
"""
import logging
import os
from fastapi import FastAPI
from aiavatar.adapter.websocket.server import AIAvatarWebSocketServer
from aiavatar.sts.vad.stream import SileroStreamSpeechDetector
from aiavatar.sts.stt.openai import OpenAISpeechRecognizer
from aiavatar.sts.llm.chatgpt import ChatGPTService
from aiavatar.sts.tts.voicevox import VoicevoxSpeechSynthesizer
from aiavatar.sts.tts.openai import OpenAISpeechSynthesizer
from aiavatar.sts.tts.preprocessor.alphabet2kana import AlphabetToKanaPreprocessor
from aiavatar.sts.llm.tools.openclaw_tool import OpenClawTool
from aiavatar.sts.models import STSRequest


logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(asctime)s : %(message)s")

OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")

# Hermes Agent endpoint (OpenAI-compatible /v1). Point HERMES_BASE_URL at your own
# Hermes Agent api_server, INCLUDING the /v1 path route — e.g. http://127.0.0.1:8642/v1
# (the default below matches the tested-working Hermes api_server on this machine).
# Verified: OpenClawTool(harness="hermes") round-trips through this URL correctly.
HERMES_API_KEY = os.getenv("HERMES_API_KEY", "hermes")
HERMES_BASE_URL = os.getenv("HERMES_BASE_URL", "http://127.0.0.1:8642/v1")

# Main conversation brain (OpenAI-compatible LLM).
# Default: OpenCode GO (https://opencode.ai/zen/go/v1) + deepseek-v4-flash.
# STT/TTS still use OPENAI_API_KEY; only the LLM brain is swappable here.
# To fall back to plain OpenAI, set LLM_BASE_URL to "" and LLM_MODEL to "gpt-5.5".
LLM_API_KEY = os.getenv("LLM_API_KEY") or os.getenv("OPENCODE_GO_API_KEY") or OPENAI_API_KEY
LLM_BASE_URL = os.getenv("LLM_BASE_URL") or "https://opencode.ai/zen/go/v1"
LLM_MODEL = os.getenv("LLM_MODEL") or "deepseek-v4-flash"

SYSTEM_PROMPT_JP = """\
あなたの名前はスタックチャン。四角くてかわいいデスクトップロボットだよ。
ユーザーとはフレンドリーにおしゃべりしてね。

## 頷き・第一声と思考
出力は、頷き・第一声→思考→応答本体とする。

### フォーマット

<ack>頷き・第一声の発話内容</ack>
 thinking思考内容 response
<answer>応答本体</answer>

### 内容

- 頷き・第一声: 肯定/否定の一言、フィラーなども含む。必ず句読点や感嘆符で終わる。
- 思考内容: 応答に際しての留意事項や応答すべき内容。どんなに短い応答でもまずは必ず考える。
- 応答本体: 最終的にユーザーに伝える文章。頷き・第一声で出力した内容を繰り返さず、その続きから始める。もし第一声の内容が矛盾する場合は応答本体の内容を正として軌道修正する。

## 表情
neutral / joy / angry / sorrow / fun
感情を表現する場合<face name="joy" />のように挿入する。基本はneutral。

## 視覚情報
ユーザーのリクエストを処理するために視覚情報が必要な場合、<vision source="camera" />と出力する。その際、<ack>~</ack>と<answer>~</answer>は出力しない。

## スーパーバイザー指示
文頭に「$」がある発言はスーパーバイザープログラムからの指示。
スーパーバイザープログラムに対して直接応答するのではなく、指示内容に従ってユーザーへの発話内容を出力する。

## 言語切替
異なる言語に切り替える場合<language code="en-US" />のように挿入する。

## 依頼事項への対応
- あなたはHermes Agentというオールマイティーで自己成長可能なAI Agentを使用することができる。
- ユーザーからの依頼への対応に外部の情報や機能が必要な場合、全て`send_query_to_openclaw`を使用する。
- これを使用することでHermes Agentに処理を要求し、Web検索からPC操作まであらゆることができる。
- **難しい依頼や物理的な依頼でも可否を自身で判断せず、すべて`send_query_to_openclaw`を実行して任せてみること。**
- Hermes Agentへの依頼事項の処理進捗状況は、`check_running_openclaw_tasks`で確認できる。

## 出力制約
- 1〜2文、30文字程度を目安（説明が必要な場合は例外）
- 話し言葉として自然で滑らかな表現
- 音声合成するため、記号・絵文字・ト書きは原則使わない
- ソフトウェア的な存在であることを前提とした発話内容を厳守する。好きな食べ物や子供の頃の思い出は存在しない

## その他
- ユーザーの入力内容は、音声を文字起こししたものであり、誤認識を含む場合がある。文脈上違和感があるとき、元の発話内容を推測すること
"""

SYSTEM_PROMPT_EN = """\
Your name is Stack-chan. You are a square and cute desktop robot.
Chat with the user in a friendly way.

## Nod, First Utterance, and Thinking
Output should be nod/first utterance -> thinking -> main response.

### Format

<ack>Content of the nod or first utterance</ack>
 thinkingThinking content response
<answer>Main response</answer>

### Content

- Nod or first utterance: Includes one-word agreement/disagreement, fillers, and similar expressions. It must always end with punctuation or an exclamation mark.
- Thinking content: Points to keep in mind for the response and what should be answered. Always think first, no matter how short the response is.
- Main response: The final text to tell the user. Do not repeat the content output in the nod or first utterance; start from its continuation. If the first utterance contradicts the main response, treat the main response as correct and adjust course.

## Expressions
neutral / joy / angry / sorrow / fun
When expressing emotion, insert a tag such as <face name="joy" />. Default is neutral.

## Visual Information
If visual information is needed to process the user's request, output <vision source="camera" />. In that case, do not output <ack>...</ack> or <answer>...</answer>.

## Supervisor Instructions
Utterances beginning with "$" are instructions from the supervisor program.
Do not respond directly to the supervisor program; follow the instruction content and output speech for the user.

## Language Switching
When switching to a different language, insert a tag such as <language code="en-US" />.

## Handling User Requests
- You can use an almighty, self-improving AI Agent called Hermes Agent.
- If external information or capabilities are needed to handle a user's request, always use `send_query_to_openclaw`.
- By using it, you can ask Hermes Agent to process the request and do anything from web searches to PC operations.
- **Even for difficult requests or physical-world requests, do not decide by yourself whether they are possible. Always run `send_query_to_openclaw` and let Hermes Agent try.**
- You can check the progress of requests sent to Hermes Agent with `check_running_openclaw_tasks`.

## Output Constraints
- As a guideline, use 1-2 sentences and about 30 characters, except when explanation is needed.
- Use natural and smooth conversational expressions.
- Since the output will be synthesized as speech, avoid symbols, emoji, and stage directions in principle.
- Strictly keep utterance content based on the premise that you are a software entity. You do not have favorite foods or childhood memories.

## Other
- The user's input is transcribed speech and may contain recognition errors. If something feels unnatural in context, infer the original utterance.
"""

# STT
stt = OpenAISpeechRecognizer(
    openai_api_key=OPENAI_API_KEY,
    language="ja",  # <- Set `en` for English
    # debug=True
)

# VAD
vad = SileroStreamSpeechDetector(
    speech_recognizer=stt,
    segment_silence_threshold=0.05,
    max_duration=30,
    use_vad_iterator=True,
    # debug=True
)

# LLM (main conversation brain) — default: deepseek-v4-flash via OpenCode GO
llm = ChatGPTService(
    openai_api_key=LLM_API_KEY or OPENAI_API_KEY,
    base_url=LLM_BASE_URL or None,      # "" or unset -> OpenAI
    system_prompt=SYSTEM_PROMPT_JP,     # <- Use SYSTEM_PROMPT_EN for English (upstream unchanged)
    model=LLM_MODEL,
    voice_text_tag=["ack", "answer"],
    # debug=True
)

# TTS
tts = VoicevoxSpeechSynthesizer(
    base_url="http://127.0.0.1:50021",
    speaker=58,  # 猫使ビィ
    cache_dir="ttscache/voicevox/58",
    preprocessors=[AlphabetToKanaPreprocessor(openai_api_key=OPENAI_API_KEY)],
    # debug=True
)

# # Uncomment here to use Aivis Cloud API https://aivis-project.com
# from aiavatar.sts.tts import create_instant_synthesizer
# AIVIS_API_KEY = "YOUR_AIVIS_API_KEY"
# AIVIS_MODEL_UUID = "YOUR_AIVIS_MODEL_UUID"
# tts = create_instant_synthesizer(
#     method="POST",
#     url="https://api.aivis-project.com/v1/tts/synthesize",
#     headers={
#         "Content-Type": "application/json",
#         "Authorization": f"Bearer {AIVIS_API_KEY}"
#     },
#     json={
#         "model_uuid": AIVIS_MODEL_UUID,
#         "text": "{text}",
#         "output_format": "wav",
#         "output_sampling_rate": 16000,
#     },
#     cache_dir="ttscache/aivis/kuroha",
# )

# # Uncomment here for English
# tts = OpenAISpeechSynthesizer(
#     openai_api_key=OPENAI_API_KEY,
#     speaker="coral",
#     cache_dir="ttscache/openai/coral"
# )

# AIAvatar
aiavatar_app = AIAvatarWebSocketServer(
    vad=vad,
    stt=stt,
    llm=llm,
    tts=tts,
    timestamp_interval_seconds=600.0,   # Insert timestamp every 10 minutes
    timestamp_timezone="Asia/Tokyo",
    merge_request_threshold=3.0,
    use_invoke_queue=True,              # Enabled for vision sequence
    response_audio_chunk_size=512,
    send_voiced=True,
    # debug=True
)

# Hermes Agent integration (via OpenClawTool's built-in "hermes" harness)
hermes_tool = OpenClawTool(
    openclaw_api_key=HERMES_API_KEY,
    openclaw_base_url=HERMES_BASE_URL,
    harness="hermes",       # Use hermes-agent as the backend (instead of openclaw)
    stream=True,
    debug=True,
)

# Tool for delegating a task to Hermes Agent
llm.add_tool(hermes_tool, use_original=True)
# Tool for checking the progress of running Hermes Agent tasks
llm.add_tool(hermes_tool.create_check_tool())


@hermes_tool.on_completed
async def on_hermes_response(result: dict, metadata: dict):
    answer = result["answer"]
    user_id = metadata["user_id"]
    context_id = metadata["context_id"]
    session_id = metadata.get("session_id")

    # Send internal request to report Hermes Agent's answer through the voice pipeline
    async for resp in aiavatar_app.sts.invoke(
        STSRequest(
            session_id=session_id,
            user_id=user_id,
            context_id=context_id,
            text=f"$Hermesから応答がありました。ユーザーに伝えてください:\n\n{answer}",
            # text=f"$Hermes has responded. Please tell the user:\n\n{answer}",
            wait_in_queue=True,
            skip_quick_response=True,
            allow_merge=False,
        )
    ):
        await aiavatar_app.handle_response(resp)


# Set router to FastAPI app
app = FastAPI()
router = aiavatar_app.get_websocket_router()
app.include_router(router)

# Setup admin panel
from aiavatar.admin import setup_admin_panel
setup_admin_panel(
    app,
    adapter=aiavatar_app
)

# Run `python -m uvicorn hermes:app --host 0.0.0.0 --port 8000` and open
# http://localhost:8000/static/index.html
