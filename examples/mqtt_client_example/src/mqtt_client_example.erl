%%
%% Copyright (c) 2021-2023 dushin.net
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
-module(mqtt_client_example).

-export([start/0]).

start() ->
    %%
    %% Start the network
    %%
    ok = start_network(maps:get(sta, config:get())),
    %%
    %% Start the MQTT client.
    %%
    Config = #{
        url => "mqtt://mqtt.eclipseprojects.io",
        connected_handler => fun handle_connected/1
    },
    {ok, _MQTT} = mqtt_client:start(Config),
    io:format("MQTT started.~n"),

    timer:sleep(infinity).

handle_connected(MQTT) ->
    Config = mqtt_client:get_config(MQTT),
    Topic = <<"atomvm/qos0">>,
    io:format("Connected to ~p~n", [maps:get(url, Config)]),
    io:format("Subscribing to ~p...~n", [Topic]),
    ok = mqtt_client:subscribe(MQTT, Topic, #{
        subscribed_handler => fun handle_subscribed/2,
        data_handler => fun handle_data/3
    }).

handle_subscribed(MQTT, Topic) ->
    io:format("Subscribed to ~p.~n", [Topic]),
    io:format("Spawning publish loop on topic ~p~n", [Topic]),
    spawn(fun() -> publish_loop(MQTT, Topic, 1) end).

handle_data(_MQTT, Topic, Data) ->
    io:format("Received data on topic ~p: ~p ~n", [Topic, Data]),
    ok.

start_network(StaConfig) ->
    case network:wait_for_sta(StaConfig) of
        {ok, {Address, Netmask, Gateway}} ->
            io:format(
                "Acquired IP address: ~s Netmask: ~s Gateway: ~s~n",
                [Address, Netmask, Gateway]
            ),
            ok;
        Error ->
            throw({unable_to_start_network, Error})
    end.

publish_loop(MQTT, Topic, Seq) ->
    io:format("Publishing data on topic ~p~n", [Topic]),
    try
        Self = self(),
        HandlePublished = fun(MQTT2, Topic2, MsgId) ->
            Self ! published,
            handle_published(MQTT2, Topic2, MsgId)
        end,
        PublishOptions = #{qos => at_least_once, published_handler => HandlePublished},
        Msg = list_to_binary("echo" ++ integer_to_list(Seq)),
        _ = mqtt_client:publish(MQTT, Topic, Msg, PublishOptions),
        receive
            published ->
                ok
        after 10000 ->
            io:format("Timed out waiting for publish ack~n")
        end
    catch
        C:E:S ->
            io:format("Error in publish: ~p:~p~p~n", [C, E, S])
    end,
    timer:sleep(1000),
    publish_loop(MQTT, Topic, Seq + 1).

handle_published(MQTT, Topic, MsgId) ->
    io:format("MQTT ~p published to topic ~p msg_id=~p~n", [MQTT, Topic, MsgId]).
