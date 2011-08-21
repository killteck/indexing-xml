set client_min_messages=DEBUG1;

-- select build_xmlindex('<?xml version="1.0"?><doc at="jedna" bt="dve" ct="tri" d="4"><tag pp="neco"><pokus at="ctyri" /></tag></doc>', 'test1');
-- select build_xmlindex('<?xml version="1.0" encoding="utf-8"?><?xml-stylesheet href="latest_ob.xsl" type="text/xsl"?><current_observation version="1.0"	 xmlns:xsd="http://www.w3.org/2001/XMLSchema"	 xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"	 xsi:noNamespaceSchemaLocation="http://www.weather.gov/view/current_observation.xsd">	<credit>NOAAs National Weather Service</credit>	<credit_URL>http://weather.gov/</credit_URL>	<image>		<url>http://weather.gov/images/xml_logo.gif</url>		<title>NOAAs National Weather Service</title>		<link>http://weather.gov</link>	</image>	<suggested_pickup>15 minutes after the hour</suggested_pickup>	<suggested_pickup_period>60</suggested_pickup_period>	<location>Stratus</location>	<station_id>32ST0</station_id>	<latitude>-19.713</latitude>	<longitude>-85.585</longitude>	<observation_time>Last Updated on Aug 11 2011, 1:00 am ST </observation_time>        <observation_time_rfc822>Thu, 11 Aug 2011 01:00:00 +0000</observation_time_rfc822>	<temperature_string>61.3 F (16.3 C)</temperature_string>	<temp_f>61.3</temp_f>	<temp_c>16.3</temp_c>	<water_temp_f>64.8</water_temp_f>	<water_temp_c>18.2</water_temp_c>	<wind_string>Southeast at 15.7 MPH (13.6 KT)</wind_string>	<wind_dir>Southeast</wind_dir>	<wind_degrees>130</wind_degrees>	<wind_mph>15.7</wind_mph>	<wind_kt>13.6</wind_kt>	<pressure_string>1019.0 mb</pressure_string>	<pressure_mb>1019.0</pressure_mb>	<dewpoint_string>59.7 F (15.4 C)</dewpoint_string>	<dewpoint_f>59.7</dewpoint_f>	<dewpoint_c>15.4</dewpoint_c>	<windchill_string>59 F (15 C)</windchill_string>      	<windchill_f>59</windchill_f>      	<windchill_c>15</windchill_c>	<mean_wave_dir>South</mean_wave_dir>	<mean_wave_degrees></mean_wave_degrees>	<disclaimer_url>http://weather.gov/disclaimer.html</disclaimer_url>	<copyright_url>http://weather.gov/disclaimer.html</copyright_url>	<privacy_policy_url>http://weather.gov/notice.html</privacy_policy_url></current_observation>', 'test2');
-- select build_xmlindex('<?xml version="1.0" encoding="ISO-8859-1"?><?xml-stylesheet href="latest_ob.xsl" type="text/xsl"?><current_observation version="1.0"	 xmlns:xsd="http://www.w3.org/2001/XMLSchema"	 xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"	 xsi:noNamespaceSchemaLocation="http://www.weather.gov/view/current_observation.xsd">	<credit>NOAAs National Weather Service</credit>	<credit_URL>http://weather.gov/</credit_URL>	<image>		<url>http://weather.gov/images/xml_logo.gif</url>		<title>NOAAs National Weather Service</title>		<link>http://weather.gov</link>	</image>	<suggested_pickup>15 minutes after the hour</suggested_pickup>	<suggested_pickup_period>60</suggested_pickup_period>	<location>San Antonio, Stinson Municipal Airport, TX</location>	<station_id>KSSF</station_id>	<latitude>29.33</latitude>	<longitude>-98.47</longitude>	<observation_time>Last Updated on Aug 11 2011, 3:53 am CDT</observation_time>        <observation_time_rfc822>Thu, 11 Aug 2011 03:53:00 -0500</observation_time_rfc822>	<weather>Mostly Cloudy</weather>	<temperature_string>84.0 F (28.9 C)</temperature_string>	<temp_f>84.0</temp_f>	<temp_c>28.9</temp_c>	<relative_humidity>74</relative_humidity>	<wind_string>from the Southeast at 17.3 gusting to 21.9 MPH (15 gusting to 19 KT)</wind_string>	<wind_dir>Southeast</wind_dir>	<wind_degrees>140</wind_degrees>	<wind_mph>17.3</wind_mph>	<wind_gust_mph>21.9</wind_gust_mph>	<wind_kt>15</wind_kt>	<wind_gust_kt>19</wind_gust_kt>	<pressure_string>1008.0 mb</pressure_string>	<pressure_mb>1008.0</pressure_mb>	<pressure_in>29.81</pressure_in>	<dewpoint_string>75.0 F (23.9 C)</dewpoint_string>	<dewpoint_f>75.0</dewpoint_f>	<dewpoint_c>23.9</dewpoint_c>	<heat_index_string>92 F (33 C)</heat_index_string>      	<heat_index_f>92</heat_index_f>      	<heat_index_c>33</heat_index_c>	<visibility_mi>10.00</visibility_mi> 	<icon_url_base>http://weather.gov/weather/images/fcicons/</icon_url_base>	<two_day_history_url>http://www.weather.gov/data/obhistory/KSSF.html</two_day_history_url>	<icon_url_name>nbkn.jpg</icon_url_name>	<ob_url>http://www.nws.noaa.gov/data/METAR/KSSF.1.txt</ob_url>	<disclaimer_url>http://weather.gov/disclaimer.html</disclaimer_url>	<copyright_url>http://weather.gov/disclaimer.html</copyright_url>	<privacy_policy_url>http://weather.gov/notice.html</privacy_policy_url></current_observation>', 'test3');
select build_xmlindex($xml$<?xml version="1.0" encoding="utf-8"?>
<books>
	<book>
		<author>
			<firstname>Quanzhong</firstname>
			<lastname>Li</lastname>
		</author>
		<title>XML indexing</title>
		<price unit = "USD">120</price>
	</book>
	<book>
		<author>
			<firstname>Bongki</firstname>
			<lastname>Moon</lastname>
		</author>
		<title>javelina javelina</title>
		<price unit = "USD" test = "AM">100</price>
	</book>
</books>$xml$, 'test4', true);

-- known problem of LibXML to distinguish between <a /> and <a></a> on same level
SELECT build_xmlindex($xml$
<doc>
   <e1   />
   <e2   ></e2>
   <e3    name = "elem3"   id="elem3"    />
   <e4    name="elem4"   id="elem4"    ></e4>
   <e5 a:attr="out" b:attr="sorted" attr2="all" attr="I'm"
       xmlns:b="http://www.ietf.org"
       xmlns:a="http://www.w3.org"
       xmlns="http://www.uvic.ca"/>
   <e6 xmlns="" xmlns:a="http://www.w3.org">
       <e7 xmlns="http://www.ietf.org">
           <e8 xmlns="" xmlns:a="http://www.w3.org">
               <e9 xmlns="" xmlns:a="http://www.ietf.org"/>
           </e8>
       </e7>
   </e6>
</doc>$xml$, 'namespaces', true);