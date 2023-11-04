#!/bin/bash

# Define the list of parameter sets
parameter_sets=(
  "5 1 1 0 1"
  "5 1 5 0 1"
  "5 5 1 0 1"
  "5 5 5 0 1"
  "10 1 1 0 1"
  "10 1 5 0 1"
  "10 1 10 0 1"
  "10 5 1 0 1"
  "10 5 5 0 1"
  "10 5 10 0 1"
  "10 10 1 0 1"
  "10 10 5 0 1"
  "10 10 10 0 1"
  "5 1 1 200 1"
  "5 1 5 200 1"
  "5 5 1 200 1"
  "5 5 5 200 1"
  "10 1 1 200 1"
  "10 1 5 200 1"
  "10 1 10 200 1"
  "10 5 1 200 1"
  "10 5 5 200 1"
  "10 5 10 200 1"
  "10 10 1 200 1"
  "10 10 5 200 1"
  "10 10 10 200 1"
  "5 1 1 400 1"
  "5 1 5 400 1"
  "5 5 1 400 1"
  "5 5 5 400 1"
  "10 1 1 400 1"
  "10 1 5 400 1"
  "10 1 10 400 1"
  "10 5 1 400 1"
  "10 5 5 400 1"
  "10 5 10 400 1"
  "10 10 1 400 1"
  "10 10 5 400 1"
  "10 10 10 400 1"
)

# Output CSV file
output_csv="results.csv"

# Write the header to the CSV file
echo "B, P, C, X, N, Time" > "$output_csv"

# Function to run the program and calculate the average execution time
run_and_record() {
  local params="$1"
  local trials=5
  local total_time=0

  for ((i = 1; i <= trials; i++)); do
    # Run the program and capture execution time
    start_time=$(date +%s.%N)
    ./paster2.out $params >/dev/null
    end_time=$(date +%s.%N)
    execution_time=$(bc <<< "$end_time - $start_time")
    total_time=$(bc <<< "$total_time + $execution_time")
  done

  average_time=$(bc <<< "scale=3; $total_time / $trials")
  echo "$params, $average_time" >> "$output_csv"
}

# Iterate over parameter sets and run the program
for params in "${parameter_sets[@]}"; do
  run_and_record "$params"
done

echo "Results written to $output_csv"
