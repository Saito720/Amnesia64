file(COPY "${AMNESIA_GAME_DIRECTORY}/"
        DESTINATION ${AMNESIA_EXECUTABLE_OUTPUT_PATH}
        PATTERN "Amnesia.*" EXCLUDE
        PATTERN "Amnesia_*" EXCLUDE
        PATTERN "Launcher.*" EXCLUDE
        PATTERN "*.rar" EXCLUDE
        PATTERN "*.pdf" EXCLUDE
        PATTERN "*.dll" EXCLUDE
        PATTERN "*.exe" EXCLUDE
        PATTERN "*"
        )

