#!/bin/bash
export PATH="/Users/bank23232525/.nvm/versions/node/v20.18.1/bin:$PATH"
cd "$(dirname "$0")/../web-app"
exec npm run dev
