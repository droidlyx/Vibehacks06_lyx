# Local AI runtime

NPC negotiation uses a real local OpenAI-compatible llama.cpp HTTP server at `127.0.0.1:8066`. No paid inference API key is required. The new project does not copy Ember's dialogue plugin or gameplay source.

## Components

- Qwen/Qwen3-1.7B-GGUF, official Q8_0 weights, Apache-2.0: https://huggingface.co/Qwen/Qwen3-1.7B-GGUF
- llama.cpp b11463, MIT: https://github.com/ggml-org/llama.cpp/releases/tag/b11463
- Fresh game-side HTTP client and validated action executor in VerdantDialogue.cpp.

`Tools/setup_ai.py --llama-source <directory containing the vendor llama-server.exe and DLLs>` downloads the official model, verifies the published SHA-256, and stages vendor binaries under ignored `Local/AI`. Model weights and binaries are not put in Git. The initial workstation uses its existing third-party llama.cpp distribution as the binary source.

Run `Play.cmd` or `Start-Game.ps1`; the AI server starts hidden if its files are installed. It binds only to localhost. If inference is unavailable, the UI reports that explicitly and never fabricates an AI answer. Day-to-day NPC resource movement is deterministic gameplay logic; free conversation and negotiated choices use the model.

Supported actions are a bounded vocabulary: speak, give existing food, open/close a controlled grove, set collection share, pledge a real delivery to the tree, follow, migrate, and cease hostility. The executor checks authority, existence, distance and resources. Text alone cannot create food or complete an unperformed trip.

## Current prototype limits

The small model may misunderstand requests or refuse imperfectly. Action validation prevents invalid resource changes; failed conditions are shown separately from its speech. This is a prototype, not an unconstrained simulation of arbitrary contracts.
