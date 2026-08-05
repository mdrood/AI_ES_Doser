#pragma once

// Registers every local-dashboard HTTP route (server.on(...) calls) on the
// global `server` object. Call this once from setup(), in place of the
// block of server.on(...) calls that used to live inline in main.cpp.
void registerWebRoutes();
