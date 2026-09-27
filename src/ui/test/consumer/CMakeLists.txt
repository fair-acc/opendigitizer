# Added from the top-level directory, not from src/ui, so that it sees only what the targets propagate -- as an external
# project would.
add_executable(qa_TouchHandlerConsumer qa_TouchHandlerConsumer.cpp)
target_link_libraries(qa_TouchHandlerConsumer PRIVATE ut opendigitizer::ui-common opendigitizer-options)
add_test(NAME qa_TouchHandlerConsumer COMMAND qa_TouchHandlerConsumer)

add_executable(qa_DashboardConsumer qa_DashboardConsumer.cpp)
target_link_libraries(qa_DashboardConsumer PRIVATE ut opendigitizer::dashboard opendigitizer-options)
add_test(NAME qa_DashboardConsumer COMMAND qa_DashboardConsumer)
