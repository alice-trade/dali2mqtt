set pagination off
set confirm off

target extended-remote :3333

monitor reset halt

break esp_gcov_dump

commands
    silent

    printf "\n dumping coverage data \n"

    monitor esp gcov dump

    printf "coverage dump complete\n\n"

    monitor reset halt

    quit
end

continue