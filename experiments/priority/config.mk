# The wrapper includes the shared implementation; changes must rebuild both variants.
$(BIN_DIR)/priority/server-priorityQueue $(BIN_DIR)/priority/server-priorityChunked: src/cache_c.c
