%%
%% Copyright (c) 2021 dushin.net
%% All rights reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
-module(mqtt_example).

-export([start/0]).

start() ->
    %%
    %% Start the network
    %%
    ok = start_network(<<"my-ssid">>, <<"my-psk">>),
    %%
    %% Start the MQTT client.
    %%
    Config = #{
        url => "mqtt://mqtt.eclipseprojects.io",
        connected_handler => fun handle_connected/2
    },
    {ok, _MQTT} = mqtt:start(Config),
    io:format("MQTT started.~n"),

    loop_forever().

loop_forever() ->
    receive
        halt -> halt
    end.

%%
%% connected callback.  This function will be called
%%
handle_connected(MQTT, Url) ->
    Topic = <<"atomvm/qos0">>,
    io:format("Connected to ~p; subscribing to ~p...~n", [Url, Topic]),
    ok = mqtt:subscribe(MQTT, Topic, #{
        qos => 0,
        subscribed_handler => fun handle_subscribed/2,
        data_handler => fun handle_data/3
    }).

handle_subscribed(MQTT, Topic) ->
    io:format("Subscribed to ~p.~n", [Topic]),
    io:format("Publishing data on topic ~p~n", [Topic]),
    _ = mqtt:publish(MQTT, Topic, term_to_binary(echo)).

handle_data(MQTT, Topic, Data) ->
    io:format("Received data on topic ~p: ~p ~n", [Topic, binary_to_term(Data)]),
    timer:sleep(5000),
    io:format("Publishing data on topic ~p~n", [Topic]),
    _ = mqtt:publish(MQTT, Topic, term_to_binary(echo)).


start_network(SSID, Psk) ->
    case network_fsm:wait_for_sta([{ssid, SSID}, {psk, Psk}]) of
        {ok, {Address, Netmask, Gateway}} ->
            io:format(
                "Acquired IP address: ~s Netmask: ~s Gateway: ~s~n",
                [Address, Netmask, Gateway]
            ),
            ok;
        Error ->
            throw({unable_to_start_network, Error})
    end.
