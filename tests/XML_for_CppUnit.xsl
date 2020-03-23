<?xml version="1.0" encoding='ISO-8859-1' standalone='yes'?>
<xsl:stylesheet version="1.0" xmlns:src="http://check.sourceforge.net/ns" xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:fo="http://www.w3.org/1999/XSL/Format">
	<xsl:output indent="yes"/>
	<xsl:template match="/src:testsuites">
		<xsl:element name="TestRun">
			<xsl:variable name="failed" select="count(src:suite/src:test[@result='failure'])" />
			<xsl:variable name="success" select="count(src:suite/src:test[@result='success'])" />
			<xsl:element name="FailedTests">
				<xsl:for-each select="src:suite/src:test[@result='failure']">
					<xsl:call-template name="test">
						<xsl:with-param name="id" select="position()"/>
					</xsl:call-template>
				</xsl:for-each>
			</xsl:element>
			<xsl:element name="SuccessfulTests">
				<xsl:for-each select="src:suite/src:test[@result='success']">
					<xsl:call-template name="test">
						<xsl:with-param name="id" select="position() + $failed"/>
					</xsl:call-template>
				</xsl:for-each>
			</xsl:element>
			<xsl:element name="Statistics">
				<xsl:element name="Tests">
					<xsl:value-of select="$failed + $success"/>
				</xsl:element>
				<xsl:element name="FailuresTotal">
					<xsl:value-of select="$failed"/>
				</xsl:element>
				<xsl:element name="Errors">
					<xsl:value-of select="0"/>
				</xsl:element>
				<xsl:element name="Failures">
					<xsl:value-of select="$failed"/>
				</xsl:element>
			</xsl:element>
		</xsl:element>
	</xsl:template>
	<xsl:template name="test">
		<xsl:param name="id" select="1"/>
		<xsl:element name="Test">
			<xsl:attribute name="id">
				<xsl:value-of select = "$id" />
			</xsl:attribute>
			<xsl:element name="Name">
				<xsl:value-of select="concat(../src:title, ':', src:id)"/>
			</xsl:element>
		</xsl:element>
	</xsl:template>
</xsl:stylesheet>
