// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/fe/fe-core/src/main/java/org/apache/doris/analysis/InformationFunction.java

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package com.starrocks.analysis;

import com.starrocks.catalog.Type;
import com.starrocks.cluster.ClusterNamespace;
import com.starrocks.common.AnalysisException;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.analyzer.ExprVisitor;
import com.starrocks.thrift.TExprNode;
import com.starrocks.thrift.TExprNodeType;
import com.starrocks.thrift.TInfoFunc;

public class InformationFunction extends Expr {
    private final String funcType;
    private long intValue;
    private String strValue;

    // First child is the comparison expr which should be in [lowerBound, upperBound].
    public InformationFunction(String funcType) {
        this.funcType = funcType;
    }

    public InformationFunction(String funcType, String strValue, long intValue) {
        this.funcType = funcType;
        this.strValue = strValue;
        this.intValue = intValue;
    }

    protected InformationFunction(InformationFunction other) {
        super(other);
        funcType = other.funcType;
        intValue = other.intValue;
        strValue = other.strValue;
    }

    @Override
    public Expr clone() {
        return new InformationFunction(this);
    }

    @Override
    protected void analyzeImpl(Analyzer analyzer) throws AnalysisException {
        if (funcType.equalsIgnoreCase("DATABASE") || funcType.equalsIgnoreCase("SCHEMA")) {
            type = Type.VARCHAR;
            strValue = ClusterNamespace.getNameFromFullName(analyzer.getDefaultDb());
        } else if (funcType.equalsIgnoreCase("USER")) {
            type = Type.VARCHAR;
            strValue = ConnectContext.get().getUserIdentity().toString();
        } else if (funcType.equalsIgnoreCase("CURRENT_USER")) {
            type = Type.VARCHAR;
            strValue = ConnectContext.get().getCurrentUserIdentity().toString();
        } else if (funcType.equalsIgnoreCase("CONNECTION_ID")) {
            type = Type.BIGINT;
            intValue = analyzer.getConnectId();
            strValue = "";
        }
    }

    public String getFuncType() {
        return funcType;
    }

    public void setIntValue(long intValue) {
        this.intValue = intValue;
    }

    public long getIntValue() {
        return intValue;
    }

    public void setStrValue(String strValue) {
        this.strValue = strValue;
    }

    public String getStrValue() {
        return strValue;
    }

    @Override
    protected void toThrift(TExprNode msg) {
        msg.node_type = TExprNodeType.INFO_FUNC;
        msg.info_func = new TInfoFunc(intValue, strValue);
    }

    @Override
    public String toSqlImpl() {
        return funcType + "()";
    }

    @Override
    public boolean isVectorized() {
        return true;
    }

    /**
     * Below function is added by new analyzer
     */
    @Override
    public <R, C> R accept(ExprVisitor<R, C> visitor, C context) {
        return visitor.visitInformationFunction(this, context);
    }
}
